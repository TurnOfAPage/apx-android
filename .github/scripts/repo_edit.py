#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 G · 接线）：LocalSend 互传 MVP1 接入（issue#45）。
  - Manifest：CHANGE_WIFI_MULTICAST_STATE + FOREGROUND_SERVICE_DATA_SYNC 权限；
    LocalSendService 注册（dataSync 类型，紧随 FloatingBallService 块）
  - page_settings：groupRemote（遥控·工具二级页）尾部加「互传（LocalSend）」开关行
  - SettingsFragment：开关读写 pref localsend_enabled，开=启动服务关=停止（复用批E apxPrefs）
  - CHANGELOG 补条
新文件三件套已直推：LocalSendProtocol / LocalSendHttpServer / LocalSendService。

⚠ 锚点铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

LOCALSEND_ROW = """        <!-- 互传（LocalSend）：局域网跨生态收文件（issue#45 · MVP1），默认关 -->
        <LinearLayout
            android:id="@+id/rowLocalSend"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:gravity="center_vertical"
            android:padding="12dp"
            android:background="@drawable/bg_card"
            android:layout_marginBottom="6dp">

            <TextView
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="互传（LocalSend）\n电脑/其他平台 LocalSend 直发，文件落 Download/LocalSend"
                android:textColor="#DD000000"
                android:textSize="14sp" />

            <Switch
                android:id="@+id/switchLocalSend"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:checked="false" />
        </LinearLayout>

"""

KOTLIN_LS = """        // 批G：LocalSend 互传开关（issue#45）——开=启动接收前台服务，关=停止
        val localSendSwitch = act.findViewById<android.widget.Switch>(R.id.switchLocalSend)
        localSendSwitch?.isChecked = apxPrefs.getBoolean("localsend_enabled", false)
        localSendSwitch?.setOnCheckedChangeListener { _, isChecked ->
            apxPrefs.edit().putBoolean("localsend_enabled", isChecked).apply()
            if (isChecked) com.allperiph.localsend.LocalSendService.start(act)
            else com.allperiph.localsend.LocalSendService.stop(act)
        }
"""

EDITS = [
    ("android/app/src/main/AndroidManifest.xml", [
        ("""    <uses-permission android:name="android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS" />
""",
         """    <uses-permission android:name="android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS" />

    <!-- LocalSend 互传：UDP 多播需 MulticastLock，否则 Wi-Fi 省电丢 announce -->
    <uses-permission android:name="android.permission.CHANGE_WIFI_MULTICAST_STATE" />
    <!-- LocalSend 接收前台服务（Android 14+ dataSync 类型必需） -->
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_DATA_SYNC" />
""", 1),
        ("""        <service
            android:name=".ui.FloatingBallService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="connectedDevice" />
""",
         """        <service
            android:name=".ui.FloatingBallService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="connectedDevice" />

        <!-- LocalSend 互传接收（issue#45 MVP1）：UDP 发现 + HTTP 接收，落 Download/LocalSend -->
        <service
            android:name=".localsend.LocalSendService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="dataSync" />
""", 1),
    ]),
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("        </LinearLayout><!-- /groupRemote -->\n",
         LOCALSEND_ROW + """        </LinearLayout><!-- /groupRemote -->
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""        cursorLockSwitch?.setOnCheckedChangeListener { _, isChecked ->
            apxPrefs.edit().putBoolean("cursor_lock", isChecked).apply()
        }
""",
         """        cursorLockSwitch?.setOnCheckedChangeListener { _, isChecked ->
            apxPrefs.edit().putBoolean("cursor_lock", isChecked).apply()
        }
""" + KOTLIN_LS, 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  行为，App 无权干预（根治需 root evdev grab，后续另立批次）\n",
         """  行为，App 无权干预（根治需 root evdev grab，后续另立批次）
- **LocalSend 互传接入（批 G · issue#45 MVP1）**：新模块 localsend（协议参数层/
  极简 HTTP server/接收端前台服务），UDP 多播 224.0.0.167:53317 发现 + register 应答，
  与官方 LocalSend 各平台互通；设置→遥控·工具 新增「互传」开关（默认关），
  文件落系统 Download/LocalSend（MediaStore，免权限），完成/失败均有通知
""", 1),
    ]),
]

def main() -> int:
    loaded = {}
    errors = []
    for path, entries in EDITS:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            errors.append(f"{path}: open failed: {e}")
            continue
        loaded[path] = (text, entries)
        for i, (old, _new, expected) in enumerate(entries, 1):
            n = text.count(old)
            if n != expected:
                errors.append(
                    f"{path} edit#{i}: anchor count={n} (expect {expected})\n---anchor---\n{old[:400]}\n---"
                )
    if errors:
        print("VALIDATION FAILED：")
        for e in errors:
            print(e)
        return 1
    for path, (text, entries) in loaded.items():
        for old, new, _expected in entries:
            text = text.replace(old, new)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"OK {path}: {len(entries)} edit(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
