package com.allperiph.ui

import android.Manifest
import android.animation.ValueAnimator
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.OrientationEventListener
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.animation.DecelerateInterpolator
import android.view.animation.OvershootInterpolator
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import com.allperiph.R
import com.allperiph.core.*
import com.allperiph.gadget.GadgetManager
import com.allperiph.shared.event.EventBus
import com.allperiph.shared.event.LogEvent
import com.allperiph.shared.inject.KeepAlive
import com.allperiph.shared.inject.KeepAliveService
import com.allperiph.shared.net.TvDiscovery
import com.allperiph.shared.util.Log
import com.allperiph.touchpad.*
import com.allperiph.ui.control.InputInjector
import com.allperiph.ui.control.InputInjector.SENSOR_TYPE
import java.io.File
import java.net.InetSocketAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.*
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * 全能外设 · AllPeriph 主界面
 *
 * 核心架构：
 *   1. 主控（MainActivity）：单 Activity，所有页面（触控板/键盘/状态/设置）都在此内
 *   2. 页面切换：ViewFlipper 管理，通过 `showPage(page)` 切换
 *   3. 生命周期：
 *      - 触控板页：横竖屏都显示（竖屏占底栏位，横屏全屏）
 *      - 键盘页：竖屏底栏位，横屏时自动展开全屏键盘（复用 TouchpadActivity）
 *      - 状态页：横竖屏都显示
 *      - 设置页：横竖屏都显示
 *   4. 输入注入：通过 InputInjector 统一处理
 *   5. 状态管理：通过 EventBus 分发
 */

/** 屏幕方向常量（与 ActivityInfo 对齐） */
private const val ORIENT_LANDSCAPE = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
private const val ORIENT_PORTRAIT  = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
private const val ORIENT_SENSOR    = ActivityInfo.SCREEN_ORIENTATION_SENSOR

/** 常量定义 */
private val TAG = "MainActivity"
private val DISCOVERY = TvDiscovery.getInstance()
private val EVENTS = EventBus.getDefault()
private val EXECUTOR = Executors.newSingleThreadExecutor()
private val rootShellExecutor = Executors.newSingleThreadExecutor()

private var rootShellProcess: Process? = null
private var rootShellHandler: Handler? = null
private var clipboardSync: ClipboardSync? = null
private var gestureEngine: GestureEngine? = null
private var touchpadFragment: TouchpadFragment? = null
private var tvControlClient: TvControllerClient? = null
private var tvDiscovery: TvDiscovery? = null
private var agentRuntime: AgentRuntime? = null
private var agentForegroundService: AgentForegroundService? = null
private var statusFragment: StatusFragment? = null
private var settingsFragment: SettingsFragment? = null
private var joystickView: JoystickView? = null
private var cursorOverlay: CursorOverlay? = null
private var expressFeedback: ExpressiveFeedback? = null
private var navBar: NavBar? = null
private var hotkeyBoard: HotkeyBoard? = null
private var touchpadView: TouchpadView? = null
private var remoteActivity: RemoteActivity? = null
private var tvScreenActivity: TvScreenActivity? = null
private var screenWidth: Int = 1200
private var screenHeight: Int = 2608
private var isLandscape = false
private val isKeyboardHidden = AtomicBoolean(true)
private val isTouchpadVisible = AtomicBoolean(true)
private var currentScreen = ""
private var isRestarting = false
private var tvDiscoveryDialog: android.app.Dialog? = null
private var tvDiscoveryListener: ((String, Int, String) -> Unit)? = null
private var tvDeviceList: List<TvDevice>? = null
private var tvDeviceListDialog: android.app.Dialog? = null
private var tvDeviceListAdapter: android.widget.ListAdapter? = null
private var tvDeviceListRecyclerView: android.widget.RecyclerView? = null
private var tvDeviceListLayoutManager: android.widget.LinearLayoutManager? = null
private var tvDeviceListScrollListener: android.widget.RecyclerView.OnScrollListener? = null
private var tvDeviceListSelectionListener: ((android.widget.RecyclerView.ViewHolder, Int) -> Unit)? = null
private var tvDeviceListClickListener: ((android.widget.RecyclerView.ViewHolder, Int) -> Unit)? = null
private var tvDeviceListLongClickListener: ((android.widget.RecyclerView.ViewHolder, Int) -> Unit)? = null
private var tvDeviceListOnItemTouchListener: android.view.View.OnTouchListener? = null
private var tvDiscoveryView: View? = null
private var tvControlClient: TvControllerClient? = null
private var tvControlServer: TvControlServer? = null
private var tvControlBroadcast: TvControlBroadcast? = null
private var tvControlMedia: TvControlMedia? = null
private var tvControlCamera: TvControlCamera? = null
private var tvControlRemote: TvControlRemote? = null
private var tvControlFeedback: TvControlFeedback? = null
private var tvControlClipboard: TvControlClipboard? = null
private var tvControlDisplay: TvControlDisplay? = null
private var tvControlGamepad: TvControlGamepad? = null
private var tvControlScreen: TvControlScreen? = null
private var tvControlSensor: TvControlSensor? = null
private var tvControlUplink: TvControlUplink? = null
private var tvControlVoice: TvControlVoice? = null
private var tvControlWifi: TvControlWifi? = null
private var tvControlBluetooth: TvControlBluetooth? = null
private var tvControlNfc: TvControlNfc? = null
private var tvControlGps: TvControlGps? = null
private var tvControlCompass: TvControlCompass? = nulln
private var tvControlAccelerometer: TvControlAccelerometer? = null
private var tvControlGyroscope: TvControlGyroscope? = null
private var tvControlMagnetometer: TvControlMagnetometer? = null
private var tvControlProximity: TvControlProximity? = null
private var tvControlLight: TvControlLight? = null
private var tvControlVibration: TvControlVibration? = null
private var tvControlHaptic: TvControlHaptic? = null
private var tvControlAudio: TvControlAudio? = null
private var tvControlVideo: TvControlVideo? = null
private var tvControlImage: TvControlImage? = null
private var tvControlFile: TvControlFile? = null
private var tvControlFolder: TvControlFolder? = null
private var tvControlDatabase: TvControlDatabase? = null
private var tvControlNetwork: TvControlNetwork? = null
private var tvControlStorage: TvControlStorage? = null
private var tvControlSecurity: TvControlSecurity? = null
private var tvControlPrivacy: TvControlPrivacy? = null
private var tvControlAccessibility: TvControlAccessibility? = null
private var tvControlDebug: TvControlDebug? = null
private var tvControlDev: TvControlDev? = null
private var tvControlTest: TvControlTest? = null
private var tvControlDemo: TvControlDemo? = null
private var tvControlBeta: TvControlBeta? = null
private var tvControlAlpha: TvControlAlpha? = null
private var tvControlNightly: TvControlNightly? = null
private var tvControlStable: TvControlStable? = null
private var tvControlRelease: TvControlRelease? = nulln
private var tvControlPreRelease: TvControlPreRelease? = null
private var tvControlBeta2: TvControlBeta2? = null
private var tvControlBeta3: TvControlBeta3? = null
private var tvControlBeta4: TvControlBeta4? = null
private var tvControlBeta5: TvControlBeta5? = null
private var tvControlBeta6: TvControlBeta6? = null
private var tvControlBeta7: TvControlBeta7? = null
private var tvControlBeta8: TvControlBeta8? = null
private var tvControlBeta9: TvControlBeta9? = null
private var tvControlBeta10: TvControlBeta10? = null
private var tvControlBeta11: TvControlBeta11? = null
private var tvControlBeta12: TvControlBeta12? = null
private var tvControlBeta13: TvControlBeta13? = null
private var tvControlBeta14: TvControlBeta14? = null
private var tvControlBeta15: TvControlBeta15? = null
private var tvControlBeta16: TvControlBeta16? = null
private var tvControlBeta17: TvControlBeta17? = null
private var tvControlBeta18: TvControlBeta18? = null
private var tvControlBeta19: TvControlBeta19? = null
private var tvControlBeta20: TvControlBeta20? = null
private var tvControlBeta21: TvControlBeta21? = null
private var tvControlBeta22: TvControlBeta22? = null
private var tvControlBeta23: TvControlBeta23? = null
private var tvControlBeta24: TvControlBeta24? = null
private var tvControlBeta25: TvControlBeta25? = null
private var tvControlBeta26: TvControlBeta26? = null
private var tvControlBeta27: TvControlBeta27? = null
private var tvControlBeta28: TvControlBeta28? = null
private var tvControlBeta29: TvControlBeta29? = null
private var tvControlBeta30: TvControlBeta30? = null
private var tvControlBeta31: TvControlBeta31? = null
private var tvControlBeta32: TvControlBeta32? = null
private var tvControlBeta33: TvControlBeta33? = null
private var tvControlBeta34: TvControlBeta34? = null
private var tvControlBeta35: TvControlBeta35? = null
private var tvControlBeta36: TvControlBeta36? = null
private var tvControlBeta37: TvControlBeta37? = null
private var tvControlBeta38: TvControlBeta38? = null
private var tvControlBeta39: TvControlBeta39? = null
private var tvControlBeta40: TvControlBeta40? = null
private var tvControlBeta41: TvControlBeta41? = null
private var tvControlBeta42: TvControlBeta42? = null
private var tvControlBeta43: TvControlBeta43? = null
private var tvControlBeta44: TvControlBeta44? = null
private var tvControlBeta45: TvControlBeta45? = null
private var tvControlBeta46: TvControlBeta46? = null
private var tvControlBeta47: TvControlBeta47? = null
private var tvControlBeta48: TvControlBeta48? = null
private var tvControlBeta49: TvControlBeta49? = null
private var tvControlBeta50: TvControlBeta50? = null
private var tvControlBeta51: TvControlBeta51? = null
private var tvControlBeta52: TvControlBeta52? = null
private var tvControlBeta53: TvControlBeta53? = null
private var tvControlBeta54: TvControlBeta54? = null
private var tvControlBeta55: TvControlBeta55? = null
private var tvControlBeta56: TvControlBeta56? = null
private var tvControlBeta57: TvControlBeta57? = null
private var tvControlBeta58: TvControlBeta58? = null
private var tvControlBeta59: TvControlBeta59? = null
private var tvControlBeta60: TvControlBeta60? = null
private var tvControlBeta61: TvControlBeta61? = null
private var tvControlBeta62: TvControlBeta62? = null
private var tvControlBeta63: TvControlBeta63? = null
private var tvControlBeta64: TvControlBeta64? = null
private var tvControlBeta65: TvControlBeta65? = null
private var tvControlBeta66: TvControlBeta66? = null
private var tvControlBeta67: TvControlBeta67? = null
private var tvControlBeta68: TvControlBeta68? = null
private var tvControlBeta69: TvControlBeta69? = null
private var tvControlBeta70: TvControlBeta70? = null
private var tvControlBeta71: TvControlBeta71? = null
private var tvControlBeta72: TvControlBeta72? = null
private var tvControlBeta73: TvControlBeta73? = null
private var tvControlBeta74: TvControlBeta74? = null
private var tvControlBeta75: TvControlBeta75? = null
private var tvControlBeta76: TvControlBeta76? = null
private var tvControlBeta77: TvControlBeta77? = null
private var tvControlBeta78: TvControlBeta78? = null
private var tvControlBeta79: TvControlBeta79? = null
private var tvControlBeta80: TvControlBeta80? = null
private var tvControlBeta81: TvControlBeta81? = null
private var tvControlBeta82: TvControlBeta82? = null
private var tvControlBeta83: TvControlBeta83? = null
private var tvControlBeta84: TvControlBeta84? = null
private var tvControlBeta85: TvControlBeta85? = null
private var tvControlBeta86: TvControlBeta86? = null
private var tvControlBeta87: TvControlBeta87? = null
private var tvControlBeta88: TvControlBeta88? = null
private var tvControlBeta89: TvControlBeta89? = null
private var tvControlBeta90: TvControlBeta90? = null
private var tvControlBeta91: TvControlBeta91? = null
private var tvControlBeta92: TvControlBeta92? = null
private var tvControlBeta93: TvControlBeta93? = null
private var tvControlBeta94: TvControlBeta94? = null
private var tvControlBeta95: TvControlBeta95? = null
private var tvControlBeta96: TvControlBeta96? = null
private var tvControlBeta97: TvControlBeta97? = null
private var tvControlBeta98: TvControlBeta98? = null
private var tvControlBeta99: TvControlBeta99? = null
private var tvControlBeta100: TvControlBeta100? = null
