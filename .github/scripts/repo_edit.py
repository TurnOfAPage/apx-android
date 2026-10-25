#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 S-1 · IME 输入法核心对，issue 实测「app 失焦无法用键盘输入法」）：
  PhysKeyBridge：
    - companion 挂 current 全局引用 + attach 登记
    - forward/forwardImpl 加 imeBypass 参数（IME 场景跳过「本地输入态放行」）
    - 新增 forwardFromIme 公开入口（转发状态与窗口转发共用，无电脑时返回 false 自然放行本地）
  新文件 ApxImeService：InputMethodService——物理键 onKeyDown/onKeyUp 全量转发 PC，
    没连电脑/未登记时走 super 自然降级为本地输入；极简面板说明文案。
  接线（manifest 声明 + 设置引导）在批 S-2。

⚠ 铁律④：Kotlin 串内换行用字面 \\n（双反斜杠），本批新文件文案已规避换行。
⚠ 铁律：锚以双引号收尾先换行。铁律⑤：实证缩进逐字节进 payload。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        # 1. companion 全局引用（插类首）
        ("""internal class PhysKeyBridge(private val act: Activity) {

    private var wrapper: Window.Callback? = null
""",
         """internal class PhysKeyBridge(private val act: Activity) {

    companion object {
        /** 批S：IME 全局入口（attach 时登记，ApxImeService 复用键盘转发状态与 heldMods） */
        var current: PhysKeyBridge? = null
    }

    private var wrapper: Window.Callback? = null
""", 1),
        # 2. attach 登记
        ("""    fun attach(window: Window) {
        if (wrapper != null) return
""",
         """    fun attach(window: Window) {
        current = this
        if (wrapper != null) return
""", 1),
        # 3. forward 签名加 imeBypass 默认参
        ("    private fun forward(event: KeyEvent): Boolean {\n",
         "    private fun forward(event: KeyEvent, imeBypass: Boolean = false): Boolean {\n", 1),
        # 4. 包装层透传
        ("        val ret = forwardImpl(event)\n",
         "        val ret = forwardImpl(event, imeBypass)\n", 1),
        # 5. 规则①加 imeBypass 豁免（锚带①注释，pointer 版注释不同天然唯一）
        ("""        // ① 手机本地输入中：修饰键也放行（否则本地 Shift+方向选字会坏）
        if (act.currentFocus is EditText) return false
""",
         """        // ① 手机本地输入中：修饰键也放行（否则本地 Shift+方向选字会坏）；IME 场景强制转发
        if (!imeBypass && act.currentFocus is EditText) return false
""", 1),
        # 6. forwardFromIme 公开入口 + forwardImpl 签名（合并锚，带 forwardImpl 上下文唯一）
        ("""        return ret
    }

    private fun forwardImpl(event: KeyEvent): Boolean {
""",
         """        return ret
    }

    /** 批S：IME 转发入口——焦点在输入框也转发（IME 就是要接管打字）；状态与窗口转发共用 */
    fun forwardFromIme(event: KeyEvent): Boolean = forward(event, imeBypass = true)

    private fun forwardImpl(event: KeyEvent, imeBypass: Boolean): Boolean {
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  （功能说明/手势/链路），分组页只留控件本体；诊断输出与内嵌选择器保留（功能不减）\n",
         """  （功能说明/手势/链路），分组页只留控件本体；诊断输出与内嵌选择器保留（功能不减）
- **全能外设输入法（批 S · issue 实测失焦输入）**：新增 InputMethodService
  （ApxImeService）——切到本输入法后物理键盘在任意界面打字全量转发到电脑
  （forwardFromIme 跳过本地输入态放行），没连电脑自然降级为手机打字；
  manifest 声明与设置引导入口随批 S-2
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
