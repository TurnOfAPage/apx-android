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
3. Kotlin 里 `set*Button("中文")`、`setTitle`、`makeText`、`text = "中文"` 这类 → 只**统计并提示**，
   不让 CI 变红（历史上存量较大，先可视化再逐步收敛）。统计分两类：
   **UI 层**（有 Context，可资源化，目标 0）与**数据层**（`core/` `wireless/` `gadget/` `hid/` `audio/`
   没有 Context，那里的中文是设备名与状态串，取不到 `R.string`，属已知例外）。

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


def check_r_import(problems):
    """用了 R.* 却没 `import <namespace>.R` —— 本地没有 Android SDK，这类错误只有 CI 编译才暴露，
    跑一轮要几分钟（run#594 三个文件漏 import，整轮构建白跑）。在这里提前挡掉。

    判断依据是**调用点**而不是「文件在哪个包」：Android 的 R 类由 AGP 按模块 namespace 生成
    （app=com.allperiph、tv=com.allperiph.tv），同包文件不用 import，其余都要。
    `android.R`（系统资源）不算 —— 正则要求 R 前面不是点或标识符字符。
    """
    R_USE = re.compile(r"(?<![\w.])R\.(string|color|layout|id|drawable|style|array|dimen|raw|xml|anim|menu|plurals|mipmap|font|integer|bool)\b")
    for mod, ns in (("app", "com.allperiph"), ("tv", "com.allperiph.tv")):
        base = os.path.join(ROOT, "android", mod, "src", "main", "java")
        if not os.path.isdir(base):
            continue
        for dirpath, _d, files in os.walk(base):
            for f in files:
                if not f.endswith((".kt", ".java")):
                    continue
                p = os.path.join(dirpath, f)
                txt = open(p, encoding="utf-8", errors="replace").read()
                if not R_USE.search(txt):
                    continue
                pkg = re.search(r"^\s*package\s+([\w.]+)", txt, re.M)
                if pkg and pkg.group(1) == ns:
                    continue
                if re.search(r"^import\s+" + re.escape(ns) + r"\.R\s*$", txt, re.M):
                    continue
                if re.search(r"^import\s+" + re.escape(ns) + r"\.\*\s*$", txt, re.M):
                    continue
                problems.append("%s 用了 R.* 却没有 `import %s.R`（编译必炸）"
                                % (os.path.relpath(p, ROOT), ns))


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

    # Kotlin 侧存量统计（只提示，不判失败）：UI 调用点上的中文字面量。
    # 只认“会进界面”的调用点，Log/println 的中文不算（那是日志，不是文案）。
    # 分两类：UI 层可资源化；core/wireless/gadget/hid/audio 是**数据层**（无 Context），
    # 那里的中文是设备名/状态串（如 ControlTarget.label），技术上取不到 R.string，属已知例外。
    UI_CALL = re.compile(
        r"(?:set(Positive|Negative|Neutral)Button|setTitle|setMessage|setHint|setText"
        r"|makeText|\.text\s*=|hint\s*=|label|status)\s*[\(=]")
    DATA_LAYER = ("/core/", "/wireless/", "/gadget/", "/hid/", "/audio/")
    kt_ui, kt_data = 0, 0
    java = os.path.join(ROOT, "android", "app", "src", "main", "java")
    for dirpath, _d, files in os.walk(java):
        for f in files:
            if not f.endswith(".kt"):
                continue
            rel = dirpath.replace(os.sep, "/") + "/"
            bucket_data = any(mark in rel for mark in DATA_LAYER)
            for line in open(os.path.join(dirpath, f), encoding="utf-8"):
                if "Log." in line or "println" in line or line.lstrip().startswith(("//", "*", "/*")):
                    continue
                if not UI_CALL.search(line):
                    continue
                for m in re.finditer(r'"([^"]*)"', line):
                    if CJK.search(m.group(1)):
                        if bucket_data:
                            kt_data += 1
                        else:
                            kt_ui += 1

    check_r_import(problems)

    print("检查 %d 个布局/资源文件" % checked)
    print("Kotlin 侧硬编码中文 UI 文案（不判失败）：UI 层 %d 处（目标是 0）、"
          "数据层例外 %d 处（core/wireless/gadget/hid/audio 无 Context）" % (kt_ui, kt_data))

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
