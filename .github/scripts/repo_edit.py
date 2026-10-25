#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#4 logcat 工具 + #6 版本更新日志回填 + README 指向 #5 新表）：
  - docs/CHANGELOG.md：回填 Unreleased 与 v212~v217（内容全部来自 tag/commit 真源，逐条带 sha）
  - README.md：完整文档表加 docs/CLIENTS.md 行
  - SettingsFragment.kt（32KB）：工具小节加 Logcat 行 + showLogcat()/readLogcat() 实现
    （root 抓全进程 logcat，失败降级 --pid 本进程日志；后台线程、回填对话框、取尾 12000 字符）
  - strings.xml：ui_main_label_logcat

两阶段：①按期望次数全量校验，失配一次性报全；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

EDITS = [
    # ————— #6：CHANGELOG 回填（锚在 v211 段之前插入） —————
    ("docs/CHANGELOG.md", [
        ("""---

## v211（2026-10-24）""",
         """---

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

## v211（2026-10-24）""", 1),
    ]),
    # ————— #5 收尾：README 文档表挂 CLIENTS.md —————
    ("README.md", [
        ("| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本更新日志 |",
         """| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本更新日志 |
| [docs/CLIENTS.md](docs/CLIENTS.md) | 各端平台客户端与功能总表 |""", 1),
    ]),
    # ————— #4：工具小节加 Logcat 行 —————
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("        tvBox.addView(settingRow(getString(R.string.ui_main_label_log_view), getString(R.string.ui_common_open), accent) { showLogs() })",
         """        tvBox.addView(settingRow(getString(R.string.ui_main_label_log_view), getString(R.string.ui_common_open), accent) { showLogs() })
        // 反馈#4：环形缓冲只留「日志级别」，信息量不够 —— 这里给全进程/内核 tag 的真 logcat
        tvBox.addView(settingRow(getString(R.string.ui_main_label_logcat), getString(R.string.ui_common_open), accent) { showLogcat() })""", 1),
        # showLogcat()/readLogcat() 插在 showLogs() 之后
        ("""        AlertDialog.Builder(activity, R.style.Theme_AllPeriph_Miuix_Dialog)
            .setTitle(getString(R.string.ui_main_dialog_log, minOf(snap.size, 120)))
            .setMessage(text)
            .setPositiveButton(getString(R.string.ui_common_close), null)
            .show()
    }""",
         """        AlertDialog.Builder(activity, R.style.Theme_AllPeriph_Miuix_Dialog)
            .setTitle(getString(R.string.ui_main_dialog_log, minOf(snap.size, 120)))
            .setMessage(text)
            .setPositiveButton(getString(R.string.ui_common_close), null)
            .show()
    }

    /**
     * Logcat 系统日志（反馈#4）：showLogs() 的环形缓冲只到「日志级别」，
     * 这里经 root 抓**全进程全 tag** 的真 logcat；无 root 时降级为本进程日志
     * （App 读自己的日志不需要 READ_LOGS）。后台线程抓取，抓到回填对话框。
     */
    private fun showLogcat() {
        val dlg = AlertDialog.Builder(activity, R.style.Theme_AllPeriph_Miuix_Dialog)
            .setTitle(getString(R.string.ui_main_label_logcat))
            .setMessage("读取中…")
            .setPositiveButton(getString(R.string.ui_common_close), null)
            .show()
        kotlin.concurrent.thread(name = "apx-logcat") {
            val out = readLogcat()
            activity?.runOnUiThread {
                if (dlg.isShowing) dlg.setMessage(out.takeLast(12000))
            }
        }
    }

    private fun readLogcat(): String {
        // ① root 全量（USB 模式本就需要 root，能看到全部进程与内核 tag）
        val shell = com.allperiph.gadget.RootShell.open()
        if (shell != null) {
            val r = shell.exec("logcat -d -v time -t 2000 2>/dev/null", 8_000L)
            shell.close()
            if (r.out.isNotBlank()) return r.out
        }
        // ② 降级：本进程日志（无需 READ_LOGS 权限）
        return runCatching {
            val p = Runtime.getRuntime().exec(
                arrayOf("logcat", "-d", "-v", "time", "--pid", android.os.Process.myPid().toString(), "-t", "1500"),
            )
            p.inputStream.bufferedReader().readText()
        }.getOrElse { e -> "logcat 读取失败：${e.message}" }
    }""", 1),
    ]),
    # ————— #4：文案 —————
    ("android/app/src/main/res/values/strings.xml", [
        ("</resources>", """    <string name="ui_main_label_logcat">Logcat 系统日志</string>
</resources>""", 1),
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
