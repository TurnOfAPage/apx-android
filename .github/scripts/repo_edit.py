#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#10 批 D · UI 修复）：header 标题防挤竖排（issue#43 图1）。
根因：v218 header 右侧 segPages+徽章+锁定+设置过宽，把左侧 weight=1 标题区
挤到 1 字宽，tvPageTitle 逐字竖排（「多出来的两个字符」）。
v219 已删 segPages 大幅缓解；本批加 singleLine+ellipsize 双保险，永不竖排。

⚠ 锚点铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/res/layout/header_bar.xml", [
        ("""        <TextView
            android:id="@+id/tvPageTitle"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:text="@string/ui_main_item_touchpad"
            android:textAppearance="@style/APTextTitle"
            android:textSize="24sp" />
""",
         """        <!-- 批D：单行+省略，右侧拥挤时永不逐字竖排（issue#43 图1） -->
        <TextView
            android:id="@+id/tvPageTitle"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:ellipsize="end"
            android:singleLine="true"
            android:text="@string/ui_main_item_touchpad"
            android:textAppearance="@style/APTextTitle"
            android:textSize="24sp" />
""", 1),
        ("""        <TextView
            android:id="@+id/tvPageSub"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingTop="4dp"
            android:textAppearance="@style/APTextCaption" />
""",
         """        <TextView
            android:id="@+id/tvPageSub"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:ellipsize="end"
            android:paddingTop="4dp"
            android:singleLine="true"
            android:textAppearance="@style/APTextCaption" />
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  单行提示与运行时诊断语境文字保留\n",
         """  单行提示与运行时诊断语境文字保留
- **Header 标题防挤竖排（issue#43 图1 · 批 D）**：顶栏标题/副说明加
  singleLine+ellipsize——右侧按钮拥挤时不再逐字竖排（v218 散落字符根因，
  v219 已删 segPages 分页钮大幅缓解，本条为防复发双保险）
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
