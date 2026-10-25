#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 H）：LocalSend MVP2 接线（issue#45 发送端）。
  - Manifest：SendActivity 注册（紧随 LocalSendService 块）
  - LocalSendService.onAnnounce：dedup 前回填 LocalSendClient 设备表（每次 announce 刷 lastSeen）
  - SettingsFragment：rowLocalSend 点行 → 打开发送页（Switch 独立管开关）
  - page_settings：互传行副题更新（开关=接收；点行=发送）
  - CHANGELOG 补 MVP2 条
新文件已直推：LocalSendClient / SendActivity / page_localsend_send.xml。

⚠ 锚点铁律：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/AndroidManifest.xml", [
        ("""        <!-- LocalSend 互传接收（issue#45 MVP1）：UDP 发现 + HTTP 接收，落 Download/LocalSend -->
        <service
            android:name=".localsend.LocalSendService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="dataSync" />
""",
         """        <!-- LocalSend 互传接收（issue#45 MVP1）：UDP 发现 + HTTP 接收，落 Download/LocalSend -->
        <service
            android:name=".localsend.LocalSendService"
            android:exported="false"
            android:stopWithTask="false"
            android:foregroundServiceType="dataSync" />

        <!-- LocalSend 发送页（issue#45 MVP2） -->
        <activity
            android:name=".localsend.SendActivity"
            android:exported="false"
            android:label="LocalSend 发送" />
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/localsend/LocalSendService.kt", [
        ("""        if (msg.optString("fingerprint") == LocalSendProtocol.fingerprint) return
        if (msg.optString("protocol", "http") != "http") return
        val port = msg.optInt("port", 0); if (port !in 1..65535) return
        val fp = msg.optString("fingerprint", ip)
""",
         """        if (msg.optString("fingerprint") == LocalSendProtocol.fingerprint) return
        if (msg.optString("protocol", "http") != "http") return
        val port = msg.optInt("port", 0); if (port !in 1..65535) return
        // 批H：每次 announce 都刷新发送端设备表（置于 dedup 前，lastSeen 才不过期）
        LocalSendClient.onAnnounce(msg, ip)
        val fp = msg.optString("fingerprint", ip)
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""            if (isChecked) com.allperiph.localsend.LocalSendService.start(act)
            else com.allperiph.localsend.LocalSendService.stop(act)
        }
""",
         """            if (isChecked) com.allperiph.localsend.LocalSendService.start(act)
            else com.allperiph.localsend.LocalSendService.stop(act)
        }
        // 批H：点行主体 → 发送页（与 Switch 开关互不干扰）
        act.findViewById<android.view.View>(R.id.rowLocalSend)?.setOnClickListener { v ->
            v.context.startActivity(android.content.Intent(v.context, com.allperiph.localsend.SendActivity::class.java))
        }
""", 1),
    ]),
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""                android:text="互传（LocalSend）\n电脑/其他平台 LocalSend 直发，文件落 Download/LocalSend"
                android:textColor="#DD000000"
                android:textSize="14sp" />
""",
         """                android:text="互传（LocalSend）\n开关=接收文件；点行=发送文件到电脑/其他平台"
                android:textColor="#DD000000"
                android:textSize="14sp" />
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  文件落系统 Download/LocalSend（MediaStore，免权限），完成/失败均有通知\n",
         """  文件落系统 Download/LocalSend（MediaStore，免权限），完成/失败均有通知
- **LocalSend 发送端（批 H · MVP2）**：新增 LocalSendClient（三步推送 register→
  prepare-upload（预算 sha256）→ 逐文件流式 upload）与 SendActivity 发送页；
  互传行点行进发送页（设备列表来自 announce，2s 刷新，点设备选文件多选）；
  触控开关保留为接收开关——开关=接收、点行=发送
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
