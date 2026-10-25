# 全能外设 更新日志

> 详细用法见 [`docs/使用说明.md`](./使用说明.md)；发版流程见 [`docs/RELEASE.md`](./RELEASE.md)。
> **版本线**：v0.4.0 起变更明细随 **GitHub Releases** 随 tag 自动发布（tag 为真源）；
> 本文件维护 Unreleased 与重要节点（v0.3.x 历史见下）。

---

## Unreleased（2026-10-25）

### 修复
- **USB 模式锁 2 个大核全频满载（反馈#1）**：`apx_jni.cpp` ALSA 非阻塞读写遇 EAGAIN 自旋 ——
  `nativeRead` 主机不发流时空转钉死一核、`nativeWrite` 缓冲满时 `continue` 空转钉死另一核；
  改为 EAGAIN 立即返回 / 1ms 节流重试（100ms 预算，超时丢帧保 stop() 能收线程），
  Kotlin 搬运循环同步节流（0906918、1ba2001）
- **方向锁定仍绑陀螺仪、设置入口难达（反馈#2）**：锁定中布局钉死不再跟系统配置翻面、
  `onResume` 不使能方向传感器、`setOrientLock` 直接停物理检测；竖屏顶栏 + 横屏悬浮
  右上角常驻「设置」入口（f3da1d0）

### 新增
- **Logcat 系统日志工具（反馈#4）**：设置页「工具」小节新增入口，root 抓全进程 logcat，
  无 root 降级本进程日志
- **各端平台客户端与功能总表**：新增 `docs/CLIENTS.md`（反馈#5），README 文档表已挂链
- **本文件回填 v212~v217**（反馈#6）；CI 新增常驻 `repo-edit` 大文件锚点编辑器
- **物理触摸板指针映射（反馈#8 Q1）**：蓝牙触摸板 → 电脑，hover 相对化 + 按键 + 滚轮
  直通三出口（PhysKeyBridge 增拦 dispatchGenericMotionEvent，TouchpadModule 增 feedPointer）
- **三指滑动手势（反馈#8 Q2）**：GestureEngine 三指阶梯判定 —— ≥60px 主轴明确为滑动
  （上=Alt+Tab、下=Win+D、左右=Ctrl+Win+←/→），[12,60) 保留中键按住、点按仍为中键
- **横屏底部分页栏常驻（反馈#9）**：横屏不再隐藏分页栏（原只能四指滑，从通知栏进来不便）；
  底部 104/90dp 留白两朝向都保留（消除 v217 横屏“留白没栏”白带），选中胶囊横屏也滑动，
  旋屏自动重算胶囊位置
- **悬浮球（反馈#7）**：独立 overlay 触控板窗口 —— 不依赖前台 Activity 焦点，
  HyperOS 小窗失焦不派发输入的绕行方案；小球单击展开/收起、拖动移位、长按关闭，
  底部 300dp 面板直接喂触控板链路（单/双/三指全支持），入口在设置页「工具」小节
- **状态页并入设置（反馈#10 批 A）**：4 页 → 触控板/键盘/设置 3 页；状态块（总开关/
  传输开关/链路诊断/运行摘要）平铺进设置页顶部，顶栏「状态↔设置」分段移除，
  四指循环/侧边限宽/页签映射/标题数组全链路收编（StatusFragment 寄生渲染零改动）
- **设置页分类列表化（反馈#10 批 B，方案 B）**：原「通用/外观/遥控/关于」顶部分段
  改为 5 行分类列表（新增「帮助与说明」）→ 点行进二级页 + 返回行回首屏；
  新增 groupHelp 帮助组（项目说明/链路与延迟/手势与操作/常见问题四卡），
  进设置页一律落回分类首页
- **长描述收进帮助页（反馈#10 批 C）**：状态页「功能说明」九行与「三步上手」三行
  原文搬进帮助组新卡（功能说明 / 三步上手）；boxIdleFeatures 保留透明空壳
  （Kotlin 整卡引用：找不到会 ensureViews 失败 / lateinit NPE），三步卡无 id 直接删；
  单行提示与运行时诊断语境文字保留
- **触摸板光标锁定（批 E · issue#44）**：设置页新增「触摸板光标锁定」开关
  （默认开启，pref=cursor_lock），Pad 上系统光标不再随触控板移动、防误触；
  仅关本地 pointer，PC 转发不受影响
- **光标锁定底层 gate（批 F · issue#44）**：连上电脑且开关开时本窗隐藏系统光标
  （PointerIcon.TYPE_NULL，状态边沿去重），断开/关开关自动恢复；连上时触摸板事件
  本就全消费、不落本 app 控件；窗口外（其他 app/桌面）的系统光标与点击为系统级
  行为，App 无权干预（根治需 root evdev grab，后续另立批次）
- **LocalSend 互传接入（批 G · issue#45 MVP1）**：新模块 localsend（协议参数层/
  极简 HTTP server/接收端前台服务），UDP 多播 224.0.0.167:53317 发现 + register 应答，
  与官方 LocalSend 各平台互通；设置→遥控·工具 新增「互传」开关（默认关），
  文件落系统 Download/LocalSend（MediaStore，免权限），完成/失败均有通知
- **LocalSend 发送端（批 H · MVP2）**：新增 LocalSendClient（三步推送 register→
  prepare-upload（预算 sha256）→ 逐文件流式 upload）与 SendActivity 发送页；
  互传行点行进发送页（设备列表来自 announce，2s 刷新，点设备选文件多选）；
  触控开关保留为接收开关——开关=接收、点行=发送
- **深色模式取色修复（批 K）**：设置页分类/行/帮助卡与互传发送页的硬编码黑色
  文字全部换主题 token（md_on_surface / md_on_surface_variant，项目按 4.5:1 对比度
  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修
- **悬浮球实体化（批 M）**：球体 35% 黑半透明 → 95% 白实底 + 描边（深浅背景都醒目），
  48dp→56dp（触控目标更好点），中心点改深色；修实测「太小/像一个点」
- **诊断日志包（批 L · 临时，下版移除）**：键盘/指针/motion 三入口逐事件打点
  （keyCode/usage/mod、source/action/双指轴、焦点与分支结果）——用于定位实体键盘
  功能键、双指手势、触控板点击误触三类实测问题（logcat tag=ApxBridge）
- **手动导出日志（批 O · issue 建议）**：设置→关于与维护 新增「导出运行日志」，
  一键把本进程最近 5000 行 logcat + 设备头存为 Download/AllPeriph/log_时间戳.txt，
  反馈问题随 issue 附上即可，免接线 logcat
- **USB HID 背压修复（批 N · issue#46 日志实锤）**：host 未取走上一报告时旧实现
  直接丢帧（日志高频「写入被拒」）——现失败帧合并进 pending（位移/滚轮/按钮/媒体键，
  溢出保留），下帧自动并入 + 16ms 补发重试，连续 1s 被拒才放弃；实体触摸板位移、
  双指滚动、点击不再因背压丢失
- **横屏操作条 pill（批 P · issue 实测）**：撤回#9 底栏常驻（恢复对称隐藏，竖屏不受
  影响）；右上新增 pill 条——触控板/键盘显式切换 + 方向锁 + 设置，替代「四指滑是
  唯一换页方式」；原悬浮设置键并入 pill（永隐防重叠）
- **设置页长文收编（批 Q · issue 实测「文字说明太长」）**：通用/外观/工具组的 4 个
  说明 caption（模板/配色/背景/诊断提示）置为不可见并**原文搬进**帮助页对应卡
  （功能说明/手势/链路），分组页只留控件本体；诊断输出与内嵌选择器保留（功能不减）
- **虚拟手柄/摇杆修复（原生符号缺失 · issue#43 真机日志）**：`apx_uinput.c` 是 C 源文件，
  而 app 的 CMake 工程只 `LANGUAGES CXX` —— CMake 无法把 `.c` 映射到已启用语言，
  直接**静默跳过**：编译链接全绿、CI 全绿，但 `libapx.so` 里一个 UinputGamepad 的
  JNI 符号都没有（实测产物符号表为 0），运行期才报
  `No implementation found ... UinputGamepad.openGamepad() is the library loaded?`，
  摇杆与手柄按钮全走降级路径。修法：`project(apx_jni LANGUAGES C CXX)`（tv 工程本来就是 C，
  所以只有手机端中招）。并新增 CI 关卡「Verify native JNI symbols in APKs」——
  构建完直接拆 APK 验符号，这类"编译通过但产物缺东西"的问题以后拦在 CI 里
- **UI 一致性收尾 + 自检清单**：把仅剩的几处字面色值收进色板（主按钮按下态
  `md_primary_pressed` 此前写死 `#FF2B6BE0` 不跟主题；桌面小组件与副屏浮层的固定色
  也进 `values/` 并注明「刻意不随主题」的理由）；布局里 10 处硬编码中文文案移入
  `strings.xml`（横屏操作条复用既有 `ui_*` 资源，不重复造词）；悬浮球面板「收起」补到
  48dp 触控下限。新增 `docs/UI-CHECKLIST.md`（XML Views 版自检清单）与
  `scripts/check_ui_tokens.py`（CI 关卡：字面色值 / 硬编码文案，两类编译器都不报的问题）
- **悬浮球面板深色取色（issue#44）**：展开的触控板面板底色是 `bg_card`（= `md_surface`，
  深色下变暗），而标题/提示/收起三处文字写死 `#DD000000` / `#66000000` / `#88000000` ——
  深色模式下黑字压深底，整块面板看不清。改用 `md_on_surface` / `md_on_surface_variant` /
  `md_primary` 令牌
- **修 run#594 编译失败 + 失败可见性改进**：3 个文件（被控输入面板 / 磁贴 / 输入法面板）
  引用了 `R.string.*` 却没 `import com.allperiph.R` —— 本地没有 Android SDK，这类错误只有
  CI 编译才暴露。补 import，并在 `scripts/check_ui_tokens.py` 加关卡「用了 R.* 却没有
  import <namespace>.R」（按模块 namespace 判定，`android.R` 不算）。
  同时把构建失败的错误上报从 check-runs API 改成 **workflow command**（`::error::`）：
  原实现先用 gh api 查 check-run id 再 POST，实测在 job 进行中返回 404、annotation 一直没写上去；
  workflow command 由 GitHub 自动收集成 annotation，不需要任何 token 权限
- **状态块瘦身 + 传输开关改按钮 + 手柄页强制横屏（实机反馈）**：
  ① 撤掉「运行摘要 / 连接状态 / 跟手程度」三张卡（信息并入顶部一行），
     首屏让给真正的操作入口；链路诊断五行搬进「通用」分组的 `rowsLinkHost`（逻辑一行没改）
  ② 无线 / 蓝牙 / USB 从「标签 + Switch」大卡片行改成三个胶囊按钮，点一下开关，
     开启 = 主色实底、关闭 = 中性描边；「开哪个弹哪个」的功能分组逻辑保持不动
     新状态从 `isTransportEnabled` **回读**（开 USB 可能失败，取反会骗人）
  ③ 手柄页 `screenOrientation` 由 `sensor` 改 `landscape` —— 它的坐标本来就是按
     800×360dp 横屏核算的，竖屏进来会挤成一团；manifest 里那条指向
     `layout-land/activity_gamepad.xml` 的注释也一并改对（该文件并不存在）
- **新增 R.id 引用关卡**：代码里 `R.id.xxx` 必须在布局里有 `@+id/xxx`。
  这类问题编译器不报，只在运行时 `findViewById` 返回 null 后炸/永不刷新 ——
  本次瘦身就真踩了一次（删了 rowsLink 字段却在 buildLinkRows 里继续用）
- **修旋屏崩溃：横屏转回竖屏必崩（真机复现）**：`TouchpadSplit.restore()` 里
  `page.removeAllViews()` 只摘直接子节点（left/divider/right），而 topRow/touchArea/
  chipSection/scroll 仍挂在 left/right **内部**，紧接着 `page.addView(topRow)` 抛
  `IllegalStateException: The specified child already has a parent` → 崩在
  `ViewTreeObserver.dispatchOnGlobalLayout` 里。修法：重挂前一律先 `detach()`，
  并把「任何 addView 之前该视图已无父」写成该类不变式（split/restore 两处对称处理）。
  影响面：横屏→竖屏第一次切换即崩，App 直接退出
- **间距令牌化（issue#42「排版混乱」）**：布局 padding/margin 此前 14 种取值，收敛为
  `values/dimens.xml` 的 8 级梯度（space_2/4/6/8/12/16/20/24），归一化 28 处（10→8、14→12、
  18→16、3→4，均不超过 2dp 且以减小为主）；给悬浮底栏留的底部空白此前三页分别是
  90/104/96dp（注释还写着 88），合并为语义 token `inset_bottom_nav`
- **Kotlin 侧色值令牌化**：被控输入面板 7 色、`md_error`（删除/失败红，深色镜下提亮）、
  色点选中描边（深色下改亮描边）、桌面小组件 3 色、磁贴与 OK 按钮 —— 全部进色板；
  并修掉两处作用域错误（widget 的形参是 `ctx` 不是 `context`；`dotBg` 里没有 `act`）
- **关卡升级**：新增「布局 dp 字面量间距」硬规则；中文文案存量改**棘轮**
  （基数 536，涨则 `::warning::`），并修正统计范围只算 main 源码
- **Kotlin 侧 UI 文案入资源**：对话框标题与按钮（快捷键管理 / 自定义键盘布局 / 删除文件 /
  恢复默认…）、字段标签、EditText hint、Toast 提示、被控输入面板说明、快捷设置磁贴标签
  共 55 处从硬编码中文改为 `strings.xml` 资源（按钮词复用既有 `ui_common_*`，百分号按
  Android 规则写 `%%`）。`scripts/check_ui_tokens.py` 改为分层统计：UI 层目标 0，
  数据层（core/wireless 等无 Context 的设备名与状态串）单独列为已知例外
- **深色模式取色修复（issue#44）**：副屏 / 手柄 / 文件 / 快捷 / 本地发送 / 被控输入
  6 个 Activity 与悬浮球此前**没接** `ThemePref`，用户选「深色」时它们仍按系统 uiMode
  取色（亮底亮字看不清）——现在全部接上同一套注入，悬浮球配色也令牌化
  （`ball_bg` / `ball_text` / `ball_stroke`，深色镜下自动反转），球体 56→60dp、图标 16→18sp
- **CI 失败可见性（tooling）**：Android 构建失败时把编译错误摘录写进 check-run annotation
  （公开仓库匿名可读，logs API 要 admin 权限），并上传 build-log-android artifact；
  repo-edit 推完提交后自动 `workflow_dispatch` 点一次 Build —— GITHUB_TOKEN 推的提交
  不触发 push 工作流，此前云端编辑的代码没人编译（批 S-1 错误即由此漏出）
- **CI 步骤抗抖动（tooling · run#589 实测）**：`sdkmanager` 偶发下载损坏
  （`Error reading Zip content from a SeekableByteChannel`）时退出码 1 会把整个 android job
  判失败、后续步骤全跳过（只差一个包却让整轮白跑）。改为每个包独立重试 3 次，
  最后以**目录存在性**判定（ubuntu-latest 镜像自带大部分组件，装不上≠用不了）
- **全能外设输入法（批 S · issue 实测失焦输入）**：新增 InputMethodService
  （ApxImeService）——切到本输入法后物理键盘在任意界面打字全量转发到电脑
  （forwardFromIme 跳过本地输入态放行），没连电脑自然降级为手机打字；
  manifest 声明与设置引导入口随批 S-2
- **Header 标题防挤竖排（issue#43 图1 · 批 D）**：顶栏标题/副说明加
  singleLine+ellipsize——右侧按钮拥挤时不再逐字竖排（v218 散落字符根因，
  v219 已删 segPages 分页钮大幅缓解，本条为防复发双保险）

---

## v217（2026-10-25）

### 新增
- **物理蓝牙键盘接入（全仓库首次）**：`PhysKeyBridge` 用 `Window.Callback` 包装拦截 KeyEvent，
  经 `HidKeys` / `Uplink` 三出口上行；`ensureViews` 接线、`onDetach` 还原（e7cc350、fc3a71a）

---

## v216（2026-10-24）

### 新增
- **横屏触控板页运行时左右分栏**：`TouchpadSplit` 在 configChanges 下旋屏不重建，
  布局 pass 幂等重排；`ensureViews` 注册分栏监听（d8be865、297eda0）
- 修复转录遗漏的 SystemClock import（32efb56）

---

## v215（2026-10-24）

### 新增
- **横屏触控板页左右分栏**：左 2/3（顶部操作行 + 手势面撑满），右 1/3（快捷键），不放方向键（cb54bed）

---

## v214（2026-10-24）

### 内部
- 横竖屏改造（PR #41）合并后发版清理：删除一次性补丁脚本与工作流（83a4145、c8a402c）

---

## v213（2026-10-24）

### 新增
- **方向锁定按钮 + 横竖屏界面优化**：顶栏 / 设置双入口、锁定暂停轮询与物理检测、
  横屏状态 / 设置页限宽居中（ba5f46a，PR #40/#41）
- CI 新增一次性锚点编辑应用器（用完自毁；后常驻为 `repo-edit`，本条回填即由它完成）

---

## v212（2026-10-24）

### 修复
- **朝向循环事故止血（两核满频 / 10W）**：恢复被损坏的 MainActivity、`forcedOrient` /
  `lastOrientSwitchMs` 跨实例保持、`orientPoll` 3s re-request 冷却、被掰回 30s 放行重试（PR #39）
- **macOS 打包签名校验三连**：`codesign --deep` / `--strict` 对「exe 旁 web/」结构必误报，
  改为签名时 web 场外签完放回 + plutil 校验（06fbc7c、8e77b59、b7b27bf）

---

## v211（2026-10-24）

### 新增
- **macOS（arm64）测试版首次发布**：Release 自此带 `AllPeriph-<tag>-macos-arm64.zip`
  （含 `apxdesktop` 桌面壳 + `apxhost` + `web/` 前端），包内附未签名放行与授权指引
  - **浏览器控制面板（方案 B）**：`apxhost panel` / `./apxdesktop` 起本地 HTTP（仅 127.0.0.1）
    自动打开 `?host=1` 页面——协议与 Windows WebView2 面板同构（state/act/query/事件四通道），
    前端 `bridge.ts` 三态传输（webview / http / mock），Windows 路径零改动（PR #30）
  - **媒体通道 9502 跨平台**：connect/收发流/5s 拥塞重试去 Win 圈（6 处等价变换，Win 逐字保留），
    面板媒体泵自动建链；**麦克风桥 CoreAudio AudioQueue**；**音箱 CoreAudio Process Tap**
    （macOS 14.2+，首次需「系统音频录制」授权；DRM 保护内容可能采集为静音，系统侧行为）（PR #31/#32）
  - **9511 受控端 + APX1PC 信标**：`apxdesktop` / `apxhost panel` 自带受控端，手机「TV 控制」
    设备列表可发现这台 Mac（标为 `(PC)`）；9512 收文件 + 面板文件卡事件刷新（PR #33）
  - 开机自启（`~/Library/LaunchAgents/` plist）、媒体收流→麦克风接线、CI 纳入 apxdesktop 编译
- **发版链补 mac（PR #34）**：macos.yml 加 `tags: ['v*']` 触发，zip 改名
  `AllPeriph-<tag>-macos-arm64.zip` 上传 Release（与 build.yml release job **双向幂等**：
  谁先完成谁建 Release、后到者 `--clobber`）；build.yml Release 正文新增 macOS 章节
- **`.app` 壳（双击入口 + TCC 授权宿主）**：zip 内含 `AllPeriph.app`——strip 后统一
  ad-hoc 补签（arm64 执行要求），`plutil -lint` + `codesign --verify --strict --deep` 进 CI；
  Info.plist 挂 `NSMicrophoneUsageDescription` 与 `NSAudioCaptureUsageDescription`
  （macOS 14.2+，官方键），麦克风/系统音频的授权从此按 bundle 归属记录——此前 CLI 裸跑
  无 Info.plist 宿主的授权未知项关闭。壳 v1 无托盘菜单（退出 `pkill -x apxdesktop`），
  NSStatusItem 托盘随后续版本（需 NSApp 事件循环，运行时语义待实机）

### 文档（PR #35）
- README：产物表 `AllPeriph-vXXX-*` 五件套（替换过时 v186 手写表，`vXXX` 占位防再过时）、
  5 分钟上手加 mac 路线、源码构建加 mac 三步、已知限制加 macOS 边界条目
- `docs/使用说明.md`：8 处 mac 增量（三端表 / 安装小节 / 快速开始入口 / 被控电脑端信标 /
  文件·副屏·音箱边界 / FAQ×4 / 卸载 / 构建 / 九章入口拆 Win·mac 两节）；
  修正「Web 控制台 v117 起已下线」过时表述（Windows 桌面面板 / **macOS 另行采用浏览器面板**）

### macOS 已知边界（如实降级，非缺陷）
- 副屏不支持（虚拟显示器仅 Windows，面板显示不支持并明确报错）；发送本机文件不支持
  （浏览器无系统文件对话框；接收不受影响）；全局热键 / USB 有线暂不可用；
  无托盘菜单（`.app` 壳退出暂用 `pkill -x apxdesktop`）；ad-hoc 签名未公证；
  TCC 授权宿主已就位（Info.plist），实际弹窗归属待真机验证

---

## v210（2026-10-23）

### 修复
- **国内网络检查更新 / 下载（PR #25）**：Android `UpdateChecker.kt` 与 PC `apxhost check-update` 直连 GitHub 失败（超时/被拦）自动回退 `gh-proxy.com` 镜像（404 视为真实无 Release，不回退）；更新弹窗新增「国内高速下载」主按钮（gh-proxy 直链 `AllPeriph-<tag>-app.apk`，打不开自动退回原 Release 页），原「查看更新」页保留为次出口

---

## v209（2026-10-23）

### 新增
- **NCM 传输通道接入（Issue #14 代码侧收口）**：`TransportKind::Ncm` 工厂分支；Auto 链改为 WinUSB → NCM → Loopback（预检到网卡才尝试 NCM，无网卡不白等 accept 超时）；`apxdisp --transport ncm`（`--host` 客户端 / 省略监听，`--port` 可覆盖默认 9512）；自检新增 [7] NCM 段（PR #23）
- **Windows 真手柄注入（ViGEmBus 可选）**：0x07 手柄帧经虚拟 X360 设备注入游戏/Steam；运行时探测 `ViGEmClient.dll`，未装驱动自动回退原键盘降级，链接期零依赖；L2/R2 映射满按模拟扳机、摇杆 Y 轴方向对齐 XInput（PR #24）
- **PC 端检查更新**：`apxhost check-update` 走 WinHTTP 读 `releases/latest`，比对算法与手机端 `UpdateChecker.kt` 一致（去 v 前缀、按非数字切段、缺段补 0）；退出码 0=已是最新 / 3=发现新版本 / 1=检查失败（PR #24）
- **意见反馈 + 检查更新（关于分段）**：反馈按类型分流到 GitHub Issues（bug / 功能建议模板带 labels）；检查更新读仓库 `releases/latest`，与本机版本按数值段比较，有新版弹窗跳 Release 页下载（`ui/UpdateChecker.kt`）
- **发版流程同源**：PC 端版本号（面板关于页 / `apxhost --version` / 卸载项 `DisplayVersion`）改为发版 tag 构建时注入 `APXPC_VERSION_STRING`，与 App `versionName`、安装包版本、Release 标签完全一致；新增《发版手册》[`docs/RELEASE.md`](./RELEASE.md)

### 改进
- 设置页结构分组（通用 / 显示 / 触摸板 / 声音 / 触感 / 蓝牙 / 实验功能 / 关于），各项补说明文案；删除无后端接口的「删除账号」
- `docs/ROADMAP.md` 六处旧账对齐源码：待补表已实现（`reportKeyboard`/`reportGamepad` v184 即存在）、`TcpControlChannel`/`TcpCtrlBridge` 已移 `.attic`、`InputSink` 方案改判为已由 `core/Uplink.resolve` 满足

### 修复
- **蓝牙键盘出口**：`HidKeys.send` 长期写死 `btConnected=false` 且无 `Uplink.BLUETOOTH` 分支 —— 电脑配对蓝牙后触控板 / 多媒体 / 手柄正常，唯独按键与复制粘贴等组合键发不出去；现取 `BtHidDevice.isConnected` 真值并接入 `reportKeyboard`（rid 2）
- 主页 `isBtConnected()` 写死 `false` → 返回真实连接状态

---

## v0.3.2

维护性版本：移除「摄像头（UVC 免驱 webcam / Wi‑Fi JPEG 上行）」模块的**全部残留**代码、
UI 引用、权限与文档，并修正此前夸大该能力的说明。

### 移除
- **安卓端**：删除 `CAMERA` 权限、JNI UVC 实现、摄像头协议常量、模块标识、
  ConfigFS 布局、死代码目录 `src/disabled/camera/*`
- **PC 端**：清理面板/媒体层/信令中的摄像头注释残留
- **文档**：README 去除「四张媒体卡/实时预览」等不实描述（更正为三张：副屏/音箱/麦克风）

### 兼容性
- 无协议版本变更、无 breaking change，覆盖安装即可

---

## v0.3.1

在 v0.3.0 基础上的体验强化与问题修复。

### 新增
- **副屏/扩展屏面板直调**：投屏目标切换、分辨率四档热切换、码率三档实时调节、状态自动记忆
- **摄像头全面升级**：H264 硬编码（CPU 趋近零）、手机端参数调节、两端联动
- **手机端**：竖持=触控板、横持=键盘（物理朝向检测，系统方向锁定不影响）

### 修复
- 扩展屏运行中改分辨率花屏
- 扩展屏触摸不跟手
- 桌面镜像模式下触摸失效
- 手机端键盘入口在部分 MIUI 场景丢失
- 摄像头帧队列积压卡顿

---

## v0.3.0

副屏全面升级 + 手机端全新改版 + 电脑面板媒体全家桶。

### 亮点
- **触摸回传**：手机副屏画面上直接点、滑，电脑光标实时跟随；双指点按 = 右键
- **真·扩展屏**：配合虚拟显示器驱动（IddCx），电脑多出一块独立屏幕
- **扩展屏指针合成**：画面中实时合成鼠标指针
- **防闪烁**：修复虚拟屏静态时不出帧导致的画面闪烁，推流稳定 ~20fps
- **手机端全新改版**：状态页总开关升级为无线/蓝牙/USB 三个传输开关
- **电脑面板媒体全家桶**：副屏/音箱/麦克风/摄像头四张功能卡

### 已知限制
- 手机当会议软件麦克风需装虚拟声卡（如 VB-Cable）
- 摄像头当前为面板内预览，作为系统摄像头设备待后续版本
- 蓝牙键盘与手柄在后续版本补全
- USB 有线模式需要手机 root

---

*MIT 开源 · 零第三方依赖 · 所有通信仅限局域网，不上传任何数据*
