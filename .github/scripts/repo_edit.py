#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 O · 手动导出日志）：设置→关于与维护 新增「导出运行日志」入口。
  - page_settings：groupAbout 尾部前插 rowExportLog 行（标题+副题+箭头，深色 token）
  - SettingsFragment：点击 → 自进程 logcat（-t 5000 --pid）+ 设备头 →
    MediaStore 落 Download/AllPeriph/log_时间戳.txt → Toast 报路径
  - CHANGELOG 补条

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EXPORT_ROW = """        <!-- 手动导出 log.txt（issue 实测建议）：logcat 自进程 → Download/AllPeriph/ -->
        <LinearLayout
            android:id="@+id/rowExportLog"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:gravity="center_vertical"
            android:padding="12dp"
            android:background="@drawable/bg_card"
            android:layout_marginBottom="6dp">

            <TextView
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="导出运行日志\n一键保存 log.txt 到 Download/AllPeriph（反馈问题时附上）"
                android:textColor="@color/md_on_surface"
                android:textSize="14sp" />

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="›"
                android:textColor="@color/md_on_surface_variant"
                android:textSize="18sp" />
        </LinearLayout>

"""

EXPORT_KOTLIN = """        // 批O：手动导出运行日志（issue 建议）——自进程 logcat → Download/AllPeriph/
        act.findViewById<android.view.View>(R.id.rowExportLog)?.setOnClickListener { v ->
            android.widget.Toast.makeText(v.context, "正在收集日志…", android.widget.Toast.LENGTH_SHORT).show()
            Thread {
                val msg = try {
                    val pid = android.os.Process.myPid()
                    val p = Runtime.getRuntime().exec(arrayOf("logcat", "-d", "-v", "time", "-t", "5000", "--pid=$pid"))
                    val raw = p.inputStream.bufferedReader().readText()
                    if (raw.isBlank()) {
                        "没有可导出的日志内容"
                    } else {
                        val stamp = java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.getDefault()).format(java.util.Date())
                        val head = "=== AllPeriph 运行日志导出 ===\n时间: $stamp\n设备: ${android.os.Build.MODEL} / SDK ${android.os.Build.VERSION.SDK_INT}\n说明: 仅含本进程最近 5000 行 logcat\n\n"
                        val body = head + raw.takeLast(890_000)
                        val fileName = "log_${System.currentTimeMillis()}.txt"
                        val values = android.content.ContentValues().apply {
                            put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, fileName)
                            put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                            put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/AllPeriph")
                            put(android.provider.MediaStore.MediaColumns.IS_PENDING, 1)
                        }
                        val uri = v.context.contentResolver.insert(
                            android.provider.MediaStore.Downloads.getContentUri(android.provider.MediaStore.VOLUME_EXTERNAL_PRIMARY), values,
                        ) ?: throw IllegalStateException("insert failed")
                        v.context.contentResolver.openOutputStream(uri)!!.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                        val fin = android.content.ContentValues().apply {
                            put(android.provider.MediaStore.MediaColumns.IS_PENDING, 0)
                        }
                        v.context.contentResolver.update(uri, fin, null, null)
                        "已保存到 Download/AllPeriph/$fileName"
                    }
                } catch (e: Throwable) {
                    "导出失败: ${e.message}"
                }
                android.os.Handler(android.os.Looper.getMainLooper()).post {
                    android.widget.Toast.makeText(v.context, msg, android.widget.Toast.LENGTH_LONG).show()
                }
            }.apply { isDaemon = true; start() }
        }
"""

EDITS = [
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("        </LinearLayout><!-- /groupAbout -->\n",
         EXPORT_ROW + """        </LinearLayout><!-- /groupAbout -->
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""        // 批H：点行主体 → 发送页（与 Switch 开关互不干扰）
        act.findViewById<android.view.View>(R.id.rowLocalSend)?.setOnClickListener { v ->
            v.context.startActivity(android.content.Intent(v.context, com.allperiph.localsend.SendActivity::class.java))
        }
""",
         """        // 批H：点行主体 → 发送页（与 Switch 开关互不干扰）
        act.findViewById<android.view.View>(R.id.rowLocalSend)?.setOnClickListener { v ->
            v.context.startActivity(android.content.Intent(v.context, com.allperiph.localsend.SendActivity::class.java))
        }
""" + EXPORT_KOTLIN, 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  功能键、双指手势、触控板点击误触三类实测问题（logcat tag=ApxBridge）\n",
         """  功能键、双指手势、触控板点击误触三类实测问题（logcat tag=ApxBridge）
- **手动导出日志（批 O · issue 建议）**：设置→关于与维护 新增「导出运行日志」，
  一键把本进程最近 5000 行 logcat + 设备头存为 Download/AllPeriph/log_时间戳.txt，
  反馈问题随 issue 附上即可，免接线 logcat
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
