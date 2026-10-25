#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#9 UI 分页）：横屏底部分页栏常驻 —— 原沉浸隐藏、只能四指滑切页，
从通知栏进来不方便（用户建议上下分页按钮/侧边栏，二者取其一实现：底部分页栏常驻）。
  - applyOrientationLayout：底栏两朝向 VISIBLE；104/90dp 底部留白两朝向都保留（消白带）
  - showPage：去掉横屏提前 return，胶囊横屏也滑动
  - 旋屏后重算胶囊（替代原横屏 setIndicator(null) 清除）
  - CHANGELOG 补反馈#9

两阶段：①按期望次数全量校验，失配一次性报全；②全过才写盘。失配 → 退出非零 → 不提交。
锚点若以双引号收尾，三引号必须先换行再闭合（run#5 教训）；正文禁字面三引号（run#7 教训）。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/MainActivity.kt", [
        # 1) 底栏两朝向常驻
        ("        tabBarView.visibility = if (landscape) View.GONE else View.VISIBLE",
         """        // 反馈#9：横屏也常驻底部分页栏（原：沉浸隐藏 → 只能四指滑，从通知栏进来切页不方便）
        tabBarView.visibility = View.VISIBLE""", 1),
        # 2) 底部留白两朝向保留（原横屏清零是因栏被隐藏，栏常驻后空间正好归栏用）
        ("""        // 竖屏布局里 pageKeyboard / pageTouchpad 的 paddingBottom（104dp / 90dp）是给悬浮
        // 底栏留位的；横屏底栏隐藏、且旋屏不重建（configChanges 含 orientation → layout-land
        // 不生效），所以这里必须手动把底部留白去掉，否则横屏键盘下方会顶出一大片白
        // （真机复现：键盘内容只到约 2/3 屏高，下面 300px 空白）。
        if (::pageKeyboardView.isInitialized) {
            pageKeyboardView.setPadding(
                pageKeyboardView.paddingLeft, pageKeyboardView.paddingTop,
                pageKeyboardView.paddingRight, if (landscape) dp(2) else dp(104),
            )
        }
        if (::pageTouchpadView.isInitialized) {
            pageTouchpadView.setPadding(
                pageTouchpadView.paddingLeft, pageTouchpadView.paddingTop,
                pageTouchpadView.paddingRight, if (landscape) dp(16) else dp(90),
            )
        }""",
         """        // 反馈#9：横屏底部分页栏常驻后，底部留白两种朝向都保留 —— 空间正好被分页栏用掉，
        // 不再是 v217 那种“留白但栏被隐藏”的 300px 白带（横屏键盘/触控板下缘各让出栏位）。
        if (::pageKeyboardView.isInitialized) {
            pageKeyboardView.setPadding(
                pageKeyboardView.paddingLeft, pageKeyboardView.paddingTop,
                pageKeyboardView.paddingRight, dp(104),
            )
        }
        if (::pageTouchpadView.isInitialized) {
            pageTouchpadView.setPadding(
                pageTouchpadView.paddingLeft, pageTouchpadView.paddingTop,
                pageTouchpadView.paddingRight, dp(90),
            )
        }""", 1),
        # 3) showPage：横屏不再提前 return（胶囊要滑）
        ("""        // 横屏沉浸模式：没有底栏，靠四指左右滑换页
        if (landscape) return
        // 新页布局完成后再滑动胶囊（否则取到的还是上一页的坐标）
        findViewById<View>(R.id.contentStack).post { animateIndicator() }
""",
         """        // 反馈#9：横屏底栏常驻 → 不再提前返回，选中胶囊也跟着滑（四指滑切页保留，只是不再唯一）
        // 新页布局完成后再滑动胶囊（否则取到的还是上一页的坐标）
        findViewById<View>(R.id.contentStack).post { animateIndicator() }
""", 1),
        # 4) 旋屏后重算胶囊（原：横屏清胶囊）——锚点含 #2 加的 updateSettingsEntry()，必须对准 master
        ("""        if (landscape) navGlass.setIndicator(null)
        applyImmersive()
        applyNavBarWidth()
        updateSettingsEntry()
    }
""",
         """        // 反馈#9：横屏底栏常驻 → 旋屏后重算胶囊位置（原横屏 setIndicator(null) 清除）
        if (landscape) findViewById<View>(R.id.contentStack).post { animateIndicator() }
        applyImmersive()
        applyNavBarWidth()
        updateSettingsEntry()
    }
""", 1),
    ]),
    # ————— CHANGELOG：反馈#9 —————
    ("docs/CHANGELOG.md", [
        ("  （上=Alt+Tab、下=Win+D、左右=Ctrl+Win+←/→），[12,60) 保留中键按住、点按仍为中键\n",
         """  （上=Alt+Tab、下=Win+D、左右=Ctrl+Win+←/→），[12,60) 保留中键按住、点按仍为中键
- **横屏底部分页栏常驻（反馈#9）**：横屏不再隐藏分页栏（原只能四指滑，从通知栏进来不便）；
  底部 104/90dp 留白两朝向都保留（消除 v217 横屏“留白没栏”白带），选中胶囊横屏也滑动，
  旋屏自动重算胶囊位置
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
