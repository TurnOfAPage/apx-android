#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 F）：光标锁定底层 gate（issue#44）。
  - PhysKeyBridge.dispatchGenericMotionEvent：转发态边沿驱动 applyCursorLock——
    handled=true（连上+在管，含悬停）→ 本窗 PointerIcon.TYPE_NULL 隐藏系统光标；
    handled=false（没连/本地输入/非指针）→ 恢复；仅 pointer 源事件驱动，状态边沿才 setter
  - 本 app 控件误触已由 forwardPointer 全消费杜绝（连上时）；窗口外（其他 app）
    的系统光标与点击是系统级行为，App 无权干预（根治需 root evdev grab，另立批次）
  - 设置页副题改准语义；CHANGELOG 补条

⚠ 锚点铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

APPLY_FN = """    /** 批F：光标锁定（issue#44）——已转发（连上+在管）→ 本窗隐藏系统光标；状态边沿才 setter */
    private var lastLockApplied: Boolean? = null
    private fun applyCursorLock(lock: Boolean) {
        val on = lock && act.getSharedPreferences("apx", android.content.Context.MODE_PRIVATE)
            .getBoolean("cursor_lock", true)
        if (lastLockApplied == on) return
        lastLockApplied = on
        act.window.decorView.pointerIcon =
            if (on) android.view.PointerIcon.getSystemIcon(act, android.view.PointerIcon.TYPE_NULL)
            else null
    }

"""

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        # 1. Wrapper 的 motion 桥接：转发态驱动光标锁定
        ("""        // 反馈#8：物理触摸板也桥接 —— hover=移动、click=按键、滚轮轴=滚动（Q1）
        override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean =
            if (forwardPointer(event)) true else delegate.dispatchGenericMotionEvent(event)
""",
         """        // 反馈#8：物理触摸板也桥接 —— hover=移动、click=按键、滚轮轴=滚动（Q1）
        // 批F：光标锁定（issue#44）——已转发（连上+在管）时隐藏本窗系统光标，放行即恢复
        override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean {
            val handled = forwardPointer(event)
            val isPointer = event.source and android.view.InputDevice.SOURCE_MOUSE != 0 ||
                event.source and android.view.InputDevice.SOURCE_TOUCHPAD != 0
            if (isPointer) applyCursorLock(handled)
            return if (handled) true else delegate.dispatchGenericMotionEvent(event)
        }
""", 1),
        # 2. 新增 applyCursorLock（插在 forwardPointer 文档前，锚尾为中文注释无引号风险）
        ("""    /**
     * Q1 触摸板映射：Android 把 BT 指针设备读成系统光标，窗口收到的是
""",
         APPLY_FN + """    /**
     * Q1 触摸板映射：Android 把 BT 指针设备读成系统光标，窗口收到的是
""", 1),
    ]),
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""                android:text="触摸板光标锁定\n开启后系统光标不随触控板移动，仅转发到电脑（Pad 防误触）"
                android:textColor="#DD000000"
                android:textSize="14sp" />
""",
         """                android:text="触摸板光标锁定\n连上电脑后本窗隐藏系统光标，触控板点击只转发到电脑（Pad 防误触）"
                android:textColor="#DD000000"
                android:textSize="14sp" />
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  仅关本地 pointer，PC 转发不受影响\n",
         """  仅关本地 pointer，PC 转发不受影响
- **光标锁定底层 gate（批 F · issue#44）**：连上电脑且开关开时本窗隐藏系统光标
  （PointerIcon.TYPE_NULL，状态边沿去重），断开/关开关自动恢复；连上时触摸板事件
  本就全消费、不落本 app 控件；窗口外（其他 app/桌面）的系统光标与点击为系统级
  行为，App 无权干预（根治需 root evdev grab，后续另立批次）
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
