cmake_minimum_required(VERSION 3.20)
project(allperiph_host VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------- WebView2 SDK（仅 Windows + 开启 WebView2 面板）
if(WIN32 AND APXPC_BUILD_UI AND APXPC_BUILD_UI_WEBVIEW2)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  set(WEBVIEW2_NUGET_VERSION "1.0.4129.50")
  FetchContent_Declare(WebView2SDK
    URL      https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WEBVIEW2_NUGET_VERSION}/microsoft.web.webview2.${WEBVIEW2_NUGET_VERSION}.nupkg
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(WebView2SDK)
  foreach(_wv2_var WebView2SDK_SOURCE_DIR webview2sdk_SOURCE_DIR WEBVIEW2SDK_SOURCE_DIR)
    if(DEFINED ${_wv2_var} AND NOT "${${_wv2_var}}" STREQUAL "")
      set(WEBVIEW2_SDK_DIR "${${_wv2_var}}")
      break()
    endif()
  endforeach()
  if(NOT DEFINED WEBVIEW2_SDK_DIR OR "${WEBVIEW2_SDK_DIR}" STREQUAL "")
    file(GLOB_RECURSE WV2_H_PATH "${CMAKE_BINARY_DIR}/_deps/*/WebView2.h"
         "${CMAKE_SOURCE_DIR}/_deps/*/WebView2.h")
    if(WV2_H_PATH)
      get_filename_component(WV2_H_DIR "${WV2_H_PATH}" DIRECTORY)
      get_filename_component(WEBVIEW2_SDK_DIR "${WV2_H_DIR}" DIRECTORY)
      get_filename_component(WEBVIEW2_SDK_DIR "${WEBVIEW2_SDK_DIR}" DIRECTORY)
      get_filename_component(WEBVIEW2_SDK_DIR "${WEBVIEW2_SDK_DIR}" DIRECTORY)
    endif()
  endif()
  message(STATUS "[host] WEBVIEW2_SDK_DIR=${WEBVIEW2_SDK_DIR}")

  FetchContent_Declare(wil
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG        v1.0.240803.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_Populate(wil)
  if(NOT TARGET wil::headers)
    add_library(wil::headers INTERFACE IMPORTED)
    set_target_properties(wil::headers PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${wil_SOURCE_DIR}/include"
    )
  endif()
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

option(APXPC_BUILD_UI             "构建桌面控制面板（仅 Windows）"        "${WIN32}")
option(APXPC_BUILD_UI_WEBVIEW2    "使用 WebView2 面板（替代旧 Win32 GDI+ 自绘）" ON)
option(APXPC_BUILD_SDK            "构建第三方 SDK 静态库"                    OFF)
option(APXPC_BUILD_EXAMPLES       "构建 SDK 示例程序"                        OFF)
option(APXPC_BUILD_TOOLS          "由 pc/tools 的 CMake 调用时置 ON"         OFF)
option(APXPC_USE_LIBUSB           "优先使用 libusb 做 USB 发现/批量传输"      OFF)
option(APXPC_BUILD_WIN_BACKENDS   "启用经典/WinRT 传感器与 HID 后端（需匹配 SDK 常量）" OFF)

set(APXPC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApxShared.cmake")

if(MSVC)
  add_compile_options(/utf-8 /EHsc /W3 /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include(CheckIncludeFileCXX)
if(WIN32)
  enable_language(RC)
endif()
if(WIN32)
  check_include_file_cxx("winrt/Windows.Devices.Sensors.h" APXPC_HAVE_CPPWINRT)
  if(APXPC_HAVE_CPPWINRT)
    message(STATUS "[host] 检测到 C++/WinRT Sensors，启用 WinRT 传感器后端")
  else()
    message(STATUS "[host] 未检测到 winrt 头，仅使用经典 Sensor API (ISensorManager) 后端")
  endif()
endif()

set(LIBUSB_TARGET "")
if(APXPC_USE_LIBUSB)
  find_path(LIBUSB_INCLUDE_DIR NAMES libusb-1.0/libusb.h libusb.h)
  find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
  if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
    add_library(apx_libusb INTERFACE)
    target_include_directories(apx_libusb INTERFACE "${LIBUSB_INCLUDE_DIR}")
    target_link_libraries(apx_libusb INTERFACE "${LIBUSB_LIBRARY}")
    set(LIBUSB_TARGET apx_libusb)
    message(STATUS "[host] 启用 libusb: ${LIBUSB_LIBRARY}")
  else()
    message(STATUS "[host] 未找到 libusb，使用平台原生实现")
    set(APXPC_USE_LIBUSB OFF)
  endif()
endif()

# ---------------------------------------------------------------- 源码组织
set(APXPC_COMMON_SRC
  "${APXPC_ROOT}/src/log/log.cpp"
  "${APXPC_ROOT}/src/platform/clock.cpp"
  "${APXPC_ROOT}/src/util/crc32.cpp"
)
set(APXPC_DISCOVERY_SRC
  "${APXPC_ROOT}/src/discovery/descriptor_parse.cpp"
  "${APXPC_ROOT}/src/discovery/monitor.cpp"
)
set(APXPC_CTRL_SRC
  "${APXPC_ROOT}/src/ctrl/ctrl_frame.cpp"
  "${APXPC_ROOT}/src/ctrl/ctrl_client.cpp"
)
set(APXPC_SENSORS_SRC
  "${APXPC_ROOT}/src/sensors/sensor_reader_factory.cpp"
)
set(APXPC_SDK_SRC
  "${APXPC_ROOT}/sdk/src/allperiph_sdk.cpp"
)

if(WIN32)
  list(APPEND APXPC_DISCOVERY_SRC
    "${APXPC_ROOT}/src/discovery/usb_enum_win.cpp"
    "${APXPC_ROOT}/src/discovery/usb_types.cpp"
    "${APXPC_ROOT}/src/discovery/discovery.cpp")
  list(APPEND APXPC_CTRL_SRC
    "${APXPC_ROOT}/src/ctrl/hid_transport_win.cpp"
    "${APXPC_ROOT}/src/ctrl/bulk_transport_win.cpp")
  if(APXPC_BUILD_WIN_BACKENDS)
    list(APPEND APXPC_SENSORS_SRC
      "${APXPC_ROOT}/src/sensors/sensor_reader_win_sensorapi.cpp")
    if(APXPC_HAVE_CPPWINRT AND EXISTS "${APXPC_ROOT}/src/sensors/sensor_reader_winrt.cpp")
      list(APPEND APXPC_SENSORS_SRC "${APXPC_ROOT}/src/sensors/sensor_reader_winrt.cpp")
    endif()
  else()
    list(APPEND APXPC_SENSORS_SRC "${APXPC_ROOT}/src/sensors/sensor_reader_win_stub.cpp")
  endif()
else()
  if(LIBUSB_TARGET)
    list(APPEND APXPC_DISCOVERY_SRC "${APXPC_ROOT}/src/discovery/usb_enum_libusb.cpp")
    list(APPEND APXPC_CTRL_SRC "${APXPC_ROOT}/src/ctrl/bulk_transport_libusb.cpp")
  else()
    list(APPEND APXPC_DISCOVERY_SRC "${APXPC_ROOT}/src/discovery/usb_enum_sysfs.cpp")
  endif()
  list(APPEND APXPC_CTRL_SRC "${APXPC_ROOT}/src/ctrl/hid_transport_hidraw.cpp")
  list(APPEND APXPC_SENSORS_SRC "${APXPC_ROOT}/src/sensors/sensor_reader_iio.cpp")
endif()

# ---------------------------------------------------------------- 目标：core
add_library(apxpc_core STATIC ${APXPC_COMMON_SRC})
target_include_directories(apxpc_core PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_core PUBLIC apx_shared)
target_compile_features(apxpc_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------- 目标：discovery
add_library(apxpc_discovery STATIC ${APXPC_DISCOVERY_SRC})
target_include_directories(apxpc_discovery PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_discovery PUBLIC apxpc_core ${LIBUSB_TARGET})

# ---------------------------------------------------------------- 目标：ctrl
add_library(apxpc_ctrl STATIC ${APXPC_CTRL_SRC})
target_include_directories(apxpc_ctrl PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_ctrl PUBLIC apxpc_core apxpc_discovery ${LIBUSB_TARGET})

# ---------------------------------------------------------------- 目标：sensors
add_library(apxpc_sensors STATIC ${APXPC_SENSORS_SRC})
target_include_directories(apxpc_sensors PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_sensors PUBLIC apxpc_core)

# ---------------------------------------------------------------- 平台链接库
set(APXPC_PLATFORM_LIBS "")
if(WIN32)
  set(APXPC_PLATFORM_LIBS setupapi cfgmgr32 hid winusb user32 gdi32 comctl32 ole32 propsys)
  target_link_libraries(apxpc_discovery PUBLIC setupapi cfgmgr32 hid)
  target_link_libraries(apxpc_ctrl     PUBLIC setupapi hid winusb cfgmgr32)
  target_link_libraries(apxpc_sensors  PUBLIC ole32 propsys)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(apxpc_core PUBLIC pthread)
endif()

# ---------------------------------------------------------------- 目标：sdk
if(APXPC_BUILD_SDK)
  add_library(apxpc_sdk STATIC ${APXPC_SDK_SRC})
  target_include_directories(apxpc_sdk PUBLIC "${APXPC_ROOT}/sdk/include")
  target_link_libraries(apxpc_sdk PUBLIC apxpc_core apxpc_discovery apxpc_ctrl apxpc_sensors)
  set(APXPC_HOST_LIBS apxpc_core apxpc_discovery apxpc_ctrl apxpc_sensors apxpc_sdk)
else()
  set(APXPC_HOST_LIBS apxpc_core apxpc_discovery apxpc_ctrl apxpc_sensors)
endif()

# ---------------------------------------------------------------- 目标：常驻服务（9511 受控端 + 热键 + 托盘 + 仲裁）
set(APXPC_SERVICE_SRC
  "${APXPC_ROOT}/src/net/json.cpp"
  "${APXPC_ROOT}/src/config/app_config.cpp"
  "${APXPC_ROOT}/src/bandwidth/arbiter.cpp"
  "${APXPC_ROOT}/src/display/display_control.cpp"
  "${APXPC_ROOT}/src/hotkey/hotkey_manager.cpp"
  "${APXPC_ROOT}/src/api/action_router.cpp"
  "${APXPC_ROOT}/src/app/host_service.cpp"
  "${APXPC_ROOT}/src/tray/tray_win32.cpp"
  # ★ 开机自启：web_panel.cpp 的 tray.setAutostart 需要链接此实现
  "${APXPC_ROOT}/src/autostart.cpp"
)
add_library(apxpc_service STATIC ${APXPC_SERVICE_SRC})
target_link_libraries(apxpc_service PUBLIC apx_shared)
target_include_directories(apxpc_service PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_service PUBLIC apxpc_core apxpc_discovery apxpc_sensors ${LIBUSB_TARGET})
if(WIN32)
  # advapi32：RegOpenKeyEx/RegSetValueEx/RegDeleteValue（autostart.cpp 注册表操作）
  target_link_libraries(apxpc_service PUBLIC ws2_32 shell32 user32 ole32 advapi32)
endif()

# ---------------------------------------------------------- 目标：统一控制面（9511）
set(APXPC_WIRELESS_SRC
  "${APXPC_ROOT}/src/wireless/wireless_session.cpp"
  "${APXPC_ROOT}/src/wireless/ctrl9511.cpp"
  "${APXPC_ROOT}/src/wireless/file_receiver.cpp"
  "${APXPC_ROOT}/src/wireless/file_sender.cpp"
)
if(WIN32)
  list(APPEND APXPC_WIRELESS_SRC "${APXPC_ROOT}/src/wireless/input_injector_win.cpp"
                                  "${APXPC_ROOT}/src/wireless/input_capture_win.cpp")
elseif(APPLE)
  list(APPEND APXPC_WIRELESS_SRC "${APXPC_ROOT}/src/wireless/input_injector_mac.mm"
                                  "${APXPC_ROOT}/src/wireless/input_capture_mac.mm")
elseif(UNIX)
  list(APPEND APXPC_WIRELESS_SRC "${APXPC_ROOT}/src/wireless/input_injector_lin.cpp"
                                  "${APXPC_ROOT}/src/wireless/input_capture_lin.cpp")
endif()
add_library(apxpc_wireless STATIC ${APXPC_WIRELESS_SRC})
target_include_directories(apxpc_wireless PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_wireless PUBLIC apxpc_core)
if(WIN32)
  target_link_libraries(apxpc_wireless PUBLIC ws2_32 Iphlpapi)
elseif(APPLE)
  enable_language(OBJCXX)
  target_link_libraries(apxpc_wireless PUBLIC "-framework CoreGraphics" "-framework AppKit")
endif()

# ---------------------------------------------------------------- 目标：媒体通道
set(APXPC_MEDIA_SRC
  "${APXPC_ROOT}/src/media/media_session.cpp"
  "${APXPC_ROOT}/src/media/audio_capture.cpp"
  "${APXPC_ROOT}/src/media/mic_bridge.cpp"
  "${APXPC_ROOT}/src/media/touch_inject.cpp"
)
add_library(apxpc_media STATIC ${APXPC_MEDIA_SRC})
target_include_directories(apxpc_media PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_media PUBLIC apxpc_core)
if(WIN32)
  target_link_libraries(apxpc_media PUBLIC ws2_32 ole32 uuid)
endif()
target_link_libraries(apxpc_wireless PUBLIC apxpc_media)

# ---------------------------------------------------------------- 目标：副屏推流
set(APXDISP_BUILD_APP   OFF CACHE BOOL "" FORCE)
set(APXDISP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${APXPC_ROOT}/../display" "${CMAKE_BINARY_DIR}/apxdisp_in_host")

set(APXPC_SCREEN_SRC
  "${APXPC_ROOT}/src/media/screen_push.cpp"
)
add_library(apxpc_screen STATIC ${APXPC_SCREEN_SRC})
target_include_directories(apxpc_screen PUBLIC "${APXPC_ROOT}/include")
target_link_libraries(apxpc_screen PUBLIC apxpc_media apxdisp_pipeline)
if(WIN32)
  target_link_libraries(apxpc_screen PUBLIC winmm)
endif()

# ---------------------------------------------------------------- 目标：apxhost 可执行文件
add_executable(apxhost "${APXPC_ROOT}/src/host_main.cpp")
target_link_libraries(apxhost PRIVATE apxpc_core apxpc_discovery apxpc_sensors apxpc_service apxpc_wireless apxpc_media)
if(WIN32)
  target_link_libraries(apxhost PRIVATE ws2_32 shell32 user32 hid setupapi cfgmgr32)
endif()

# ---------------------------------------------------------------- 目标：桌面端面板
if(WIN32 AND APXPC_BUILD_UI)
  if(APXPC_BUILD_UI_WEBVIEW2)
    set(APXPC_WEBPANEL_SRC
      "${APXPC_ROOT}/src/ui/web_panel.cpp"
      "${APXPC_ROOT}/src/ui/webview2_runtime.cpp")
    add_library(apxpc_webpanel STATIC ${APXPC_WEBPANEL_SRC})
    target_include_directories(apxpc_webpanel PUBLIC "${APXPC_ROOT}/include")
    target_link_libraries(apxpc_webpanel PUBLIC apxpc_core apxpc_wireless apxpc_media apxpc_screen apxpc_service)

    if(DEFINED WEBVIEW2_SDK_DIR)
      target_include_directories(apxpc_webpanel PUBLIC
        "${WEBVIEW2_SDK_DIR}/build/native/include")
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WV2_LIB_DIR "${WEBVIEW2_SDK_DIR}/build/native/x64")
      elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(WV2_LIB_DIR "${WEBVIEW2_SDK_DIR}/build/native/x86")
      else()
        set(WV2_LIB_DIR "${WEBVIEW2_SDK_DIR}/build/native/arm64")
      endif()
      target_link_directories(apxpc_webpanel PUBLIC "${WV2_LIB_DIR}")
      target_link_libraries(apxpc_webpanel PUBLIC WebView2Loader.dll.lib)
    endif()
    if(TARGET wil::headers)
      target_link_libraries(apxpc_webpanel PUBLIC wil::headers)
    elseif(EXISTS "${wil_SOURCE_DIR}/include")
      target_include_directories(apxpc_webpanel PUBLIC "${wil_SOURCE_DIR}/include")
    endif()
    target_link_libraries(apxpc_webpanel PUBLIC user32 shell32 ole32 shlwapi winhttp)
  else()
    set(APXPC_UI_SRC
      "${APXPC_ROOT}/src/ui/panel_win32.cpp"
      "${APXPC_ROOT}/src/ui/file_panel_win32.cpp"
      "${APXPC_ROOT}/src/ui/remote_input_win32.cpp"
      "${APXPC_ROOT}/src/ui/settings_win32.cpp"
    )
    add_library(apxpc_ui STATIC ${APXPC_UI_SRC})
    target_include_directories(apxpc_ui PUBLIC "${APXPC_ROOT}/include")
    target_link_libraries(apxpc_ui PUBLIC apxpc_core apxpc_wireless apxpc_service apxpc_media apxpc_screen)
    if(WIN32)
      target_link_libraries(apxpc_ui PUBLIC comctl32 user32 shell32 gdi32 gdiplus)
    endif()
  endif()
endif()

set(APXDESKTOP_SRC "${APXPC_ROOT}/src/desktop_main.cpp")
if(WIN32)
  list(APPEND APXDESKTOP_SRC "${APXPC_ROOT}/res/apx.rc")
endif()
add_executable(apxdesktop ${APXDESKTOP_SRC})

if(APXPC_BUILD_UI AND APXPC_BUILD_UI_WEBVIEW2 AND WIN32)
  target_link_libraries(apxdesktop PRIVATE apxpc_webpanel apxpc_wireless apxpc_media apxpc_screen apxpc_service apxpc_core)
elseif(APXPC_BUILD_UI AND WIN32)
  target_link_libraries(apxdesktop PRIVATE apxpc_ui apxpc_wireless apxpc_core)
else()
  target_link_libraries(apxdesktop PRIVATE apxpc_core)
endif()

if(WIN32)
  set_target_properties(apxdesktop PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(apxdesktop PRIVATE
    "/ENTRY:mainCRTStartup"
    "/MANIFEST:EMBED,ID=1"
    "/MANIFESTINPUT:${APXPC_ROOT}/res/apx.manifest")
  target_link_libraries(apxdesktop PRIVATE ws2_32 shell32 user32 comctl32 gdi32 gdiplus setupapi cfgmgr32 advapi32)

  if(APXPC_BUILD_UI_WEBVIEW2)
    find_program(APXPC_NODE_EXE NAMES node node.exe)
    find_program(APXPC_NPM_EXE NAMES npm npm.cmd)
    set(APXPC_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../web")
    set(APXPC_WEB_DIST "${APXPC_WEB_DIR}/dist")
    set(APXPC_WEB_OUT_DIR "$<TARGET_FILE_DIR:apxdesktop>/web")
    if(APXPC_NODE_EXE AND APXPC_NPM_EXE AND EXISTS "${APXPC_WEB_DIR}/package.json")
      add_custom_command(TARGET apxdesktop PRE_BUILD
        COMMAND "${APXPC_NPM_EXE}" run build
        WORKING_DIRECTORY "${APXPC_WEB_DIR}"
        COMMENT "[host] 构建 WebView2 前端（Vite + React）"
        VERBATIM)
      add_custom_command(TARGET apxdesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APXPC_WEB_OUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${APXPC_WEB_DIST}" "${APXPC_WEB_OUT_DIR}"
        COMMENT "[host] 拷贝前端 dist → apxdesktop/web/"
        VERBATIM)
    else()
      message(WARNING "[host] WebView2 前端未构建")
    endif()
    if(DEFINED WEBVIEW2_SDK_DIR AND EXISTS "${WEBVIEW2_SDK_DIR}/build/native")
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WV2_DLL_DIR "${WEBVIEW2_SDK_DIR}/build/native/x64")
      elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(WV2_DLL_DIR "${WEBVIEW2_SDK_DIR}/build/native/x86")
      else()
        set(WV2_DLL_DIR "${WEBVIEW2_SDK_DIR}/build/native/arm64")
      endif()
      add_custom_command(TARGET apxdesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WV2_DLL_DIR}/WebView2Loader.dll"
          "$<TARGET_FILE_DIR:apxdesktop>/WebView2Loader.dll"
        COMMENT "[host] 拷贝 WebView2Loader.dll"
        VERBATIM)
    endif()
  endif()

  set(APX_PAYLOAD_DIR "${CMAKE_BINARY_DIR}/payload")
  set(APX_PAYLOAD_EXE "${APX_PAYLOAD_DIR}/apxdesktop.exe")
  file(TO_NATIVE_PATH "${APX_PAYLOAD_EXE}" APX_PAYLOAD_EXE_NATIVE)
  configure_file("${APXPC_ROOT}/res/setup_payload.rc.in"
                 "${CMAKE_BINARY_DIR}/setup_payload.rc" @ONLY)
  add_custom_command(TARGET apxdesktop POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${APX_PAYLOAD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:apxdesktop>" "${APX_PAYLOAD_EXE}"
    COMMENT "暂存安装包载荷 apxdesktop.exe")

  add_executable(apxsetup
    "${APXPC_ROOT}/src/setup_main.cpp"
    "${APXPC_ROOT}/src/ui/webview2_runtime.cpp"
    "${CMAKE_BINARY_DIR}/setup_payload.rc")
  target_include_directories(apxsetup PRIVATE "${APXPC_ROOT}/include")
  target_link_libraries(apxsetup PRIVATE shell32 ole32 shlwapi user32 advapi32 winhttp)
  set_target_properties(apxsetup PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(apxsetup PRIVATE
    "/ENTRY:mainCRTStartup"
    "/MANIFEST:EMBED,ID=1"
    "/MANIFESTINPUT:${APXPC_ROOT}/res/apx.manifest")
  add_dependencies(apxsetup apxdesktop)
  add_custom_command(TARGET apxsetup POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE_DIR:apxdesktop>/WebView2Loader.dll"
      "$<TARGET_FILE_DIR:apxsetup>/WebView2Loader.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:apxsetup>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "$<TARGET_FILE_DIR:apxdesktop>/web"
      "$<TARGET_FILE_DIR:apxsetup>/web"
    COMMENT "[host] 为 apxsetup 附带 WebView2Loader.dll + web/ 伴随文件"
    VERBATIM)
endif()

# ---------------------------------------------------------------- 示例
if(APXPC_BUILD_SDK AND APXPC_BUILD_EXAMPLES)
  add_executable(sensor_watch "${APXPC_ROOT}/sdk/examples/sensor_watch.c")
  target_link_libraries(sensor_watch PRIVATE apxpc_sdk)
  set_target_properties(sensor_watch PROPERTIES C_STANDARD 99)
endif()

# ---------------------------------------------------------------- 单元测试（离线可跑）
option(APXPC_BUILD_TESTS "构建宿主离线单元测试" ON)
if(APXPC_BUILD_TESTS)
  enable_testing()
  add_executable(arbiter_test "${APXPC_ROOT}/tests/arbiter_test.cpp")
  target_link_libraries(arbiter_test PRIVATE apxpc_service)
  if(WIN32)
    target_link_libraries(arbiter_test PRIVATE ws2_32 hid setupapi cfgmgr32)
  endif()
  add_test(NAME arbiter_test COMMAND arbiter_test)

  add_executable(utf16_convert_test "${APXPC_ROOT}/tests/utf16_convert_test.cpp")
  if(WIN32)
    target_link_libraries(utf16_convert_test PRIVATE user32 ole32)
  endif()
  add_test(NAME utf16_convert_test COMMAND utf16_convert_test)

  add_executable(ctrl_frame_test "${APXPC_ROOT}/tests/ctrl_frame_test.cpp")
  target_link_libraries(ctrl_frame_test PRIVATE apxpc_ctrl)
  if(WIN32)
    target_link_libraries(ctrl_frame_test PRIVATE ws2_32 hid setupapi cfgmgr32)
  endif()
  add_test(NAME ctrl_frame_test COMMAND ctrl_frame_test)
endif()

# ---------------------------------------------------------------- 安装
install(TARGETS apxhost apxdesktop RUNTIME DESTINATION bin)
if(APXPC_BUILD_SDK)
  install(TARGETS apxpc_sdk ARCHIVE DESTINATION lib)
  install(DIRECTORY "${APXPC_ROOT}/sdk/include/" DESTINATION include)
endif()

message(STATUS "[host] 平台=${CMAKE_SYSTEM_NAME} UI=${APXPC_BUILD_UI} libusb=${APXPC_USE_LIBUSB} shared=${APX_HAVE_SHARED}")