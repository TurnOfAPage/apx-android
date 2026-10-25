#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#7 悬浮球接线；4 个新文件已直接推送，本批只做注册/入口/文案）：
  - AndroidManifest：注册 FloatingBallService（connectedDevice 前台类型，复用已有权限）
  - SettingsFragment：工具小节加「悬浮球」行（开/关态 + 点击切换 + 重渲染）
  - strings.xml：悬浮球 6 条文案
  - CHANGELOG：Unreleased 补反馈#7

两阶段：①按期望次数全量校验，失配一次性报全；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

EDITS = [
    ("android/app/src/main/AndroidManifest.xml", [
        ("""        <!--
          ⑥ 后台服务（v1.19）：启动前台通知，保留 Binder 保活。
        -->
        <service
            android:name=".ui.AgentForegroundService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="connectedDevice" />
""",
         """        <!--
          ⑥ 后台服务（v1.19）：启动前台通知，保留 Binder 保活。
        -->
        <service
            android:name=".ui.AgentForegroundService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="connectedDevice" />

        <!-- 反馈#7：悬浮球 —— 独立 overlay 触控板窗口（HyperOS 小窗失焦不派发输入的绕行） -->
        <service
            android:name=".ui.FloatingBallService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="connectedDevice" />
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("        tvBox.addView(settingRow(getString(R.string.ui_main_label_logcat), getString(R.string.ui_common_open), accent) { showLogcat() })\n",
         """        tvBox.addView(settingRow(getString(R.string.ui_main_label_logcat), getString(R.string.ui_common_open), accent) { showLogcat() })
        // 反馈#7：悬浮球 —— 独立 overlay 触控板（绕开 HyperOS 小窗失焦不能输入）；无权限先跳授权
        tvBox.addView(
            settingRow(
                getString(R.string.ui_main_label_floating_ball),
                getString(
                    if (FloatingBallService.isRunning) R.string.ui_main_floating_ball_on
                    else R.string.ui_main_floating_ball_off,
                ),
                accent,
            ) {
                FloatingBallService.requestToggle(it.context)
                renderTv()
            },
        )
""", 1),
    ]),
    ("android/app/src/main/res/values/strings.xml", [
        ("</resources>", """    <string name="ui_main_label_floating_ball">悬浮球</string>
    <string name="ui_main_floating_ball_on">已开启</string>
    <string name="ui_main_floating_ball_off">未开启</string>
    <string name="ui_main_floating_ball_note">单击展开触控板，长按关闭</string>
    <string name="ui_main_floating_ball_hint">单指移动 · 双指滚动 · 三指滑动</string>
    <string name="ui_main_floating_ball_collapse">收起</string>
</resources>""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  旋屏自动重算胶囊位置\n",
         """  旋屏自动重算胶囊位置
- **悬浮球（反馈#7）**：独立 overlay 触控板窗口 —— 不依赖前台 Activity 焦点，
  HyperOS 小窗失焦不派发输入的绕行方案；小球单击展开/收起、拖动移位、长按关闭，
  底部 300dp 面板直接喂触控板链路（单/双/三指全支持），入口在设置页「工具」小节
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
