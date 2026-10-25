#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""UI 一致性关卡：色值令牌化 + 文案入资源（CI 用，非 0 即失败）。

## 为什么需要它
这两类问题**编译器都不报**，只会以「深色模式下看不清」「换肤没跟着变」「文案改不动」的
形式在真机上冒出来：

- **字面色值**：布局里写 `#DD000000` 之类，浅色看着正常，`values-night` 一覆盖底色就成
  黑字压深底（issue#44 的悬浮球面板就是这么来的）。色值必须经 `@color/` 令牌，
  深浅两套在 `values` / `values-night` 里成对定义。
- **硬编码文案**：`android:text="…"` 写死在布局里，等于绕开 `strings.xml`，改文案要动布局、
  也没法做多语言。UI 文案一律走 `@string/`。

## 规则与白名单
1. `res/layout*/**/*.xml`、`res/drawable/*.xml` 里出现 `#RGB`/`#ARGB` 字面色值 → 失败。
   白名单：`<vector>` 图标内的 `fillColor`/`strokeColor` —— 单色图标本来就是**遮罩**，
   颜色由使用处 `android:tint` / `setColorFilter` 决定，写黑是惯例（本仓 `ic_apx_*` 即如此）。
2. 布局里 `android:text|hint|contentDescription` 含中文 → 失败（应移入 `strings.xml`）。
3. Kotlin 里 `set*Button("中文")`、`text = "中文"` 这类 → 只**统计并提示**，不让 CI 变红。
   历史存量较大，先可视化再逐步收敛；数量下降说明在改善。

注释行（`<!-- ... -->`）不参与判定 —— 文档里举例提到色值是正常的。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "android", "app", "src", "main", "res")

CJK = re.compile(r"[\u4e00-\u9fff]")
LITERAL_COLOR = re.compile(r'"#[0-9A-Fa-f]{3,8}"')
TEXT_ATTR = re.compile(r'android:(text|hint|contentDescription)="([^"]*)"')


def strip_comments(text):
    return re.sub(r"<!--.*?-->", "", text, flags=re.S)


def xml_files():
    out = []
    for sub in ("layout", "layout-land"):
        d = os.path.join(RES, sub)
        if os.path.isdir(d):
            out += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".xml")]
    dd = os.path.join(RES, "drawable")
    if os.path.isdir(dd):
        out += [os.path.join(dd, f) for f in sorted(os.listdir(dd)) if f.endswith(".xml")]
    return out


def is_vector_masking(path):
    """单色 vector 图标：颜色写死是遮罩惯例，交给使用处 tint。"""
    if not path.endswith(".xml"):
        return False
    head = open(path, encoding="utf-8").read(400)
    return "<vector" in head


def main():
    problems = []
    checked = 0

    for p in xml_files():
        rel = os.path.relpath(p, ROOT)
        raw = open(p, encoding="utf-8").read()
        body = strip_comments(raw)
        is_layout = "/layout" in p and "drawable" not in p
        checked += 1

        if not is_vector_masking(p):
            for i, line in enumerate(body.splitlines(), 1):
                if LITERAL_COLOR.search(line):
                    problems.append("%s:%d 字面色值（应改用 @color/ 令牌）：%s"
                                    % (rel, i, line.strip()[:90]))

        if is_layout:
            for i, line in enumerate(body.splitlines(), 1):
                m = TEXT_ATTR.search(line)
                if m and CJK.search(m.group(2)):
                    problems.append("%s:%d 硬编码文案（应移入 strings.xml）：%s=\"%s…\""
                                    % (rel, i, m.group(1), m.group(2)[:24]))

    # Kotlin 侧存量统计（只提示）
    kt_hits = 0
    java = os.path.join(ROOT, "android", "app", "src", "main", "java")
    for dirpath, _d, files in os.walk(java):
        for f in files:
            if not f.endswith(".kt"):
                continue
            for line in open(os.path.join(dirpath, f), encoding="utf-8"):
                m = re.search(r'(?:set(Positive|Negative|Neutral)Button|text\s*=)\s*"([^"]*)"', line)
                if m and CJK.search(m.group(2)):
                    kt_hits += 1

    print("检查 %d 个布局/资源文件" % checked)
    print("Kotlin 侧硬编码中文 UI 文案存量（不判失败）：%d 处" % kt_hits)

    if problems:
        print("\n========== UI 一致性违规 ==========")
        for x in problems:
            print("FAIL " + x)
        print("\n修法：色值进 values/colors.xml（需要随主题变就在 values-night 里成对覆盖）；"
              "文案进 values/strings.xml 并改引 @string/。")
        return 1

    print("通过：布局与 drawable 无字面色值、无硬编码文案。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
