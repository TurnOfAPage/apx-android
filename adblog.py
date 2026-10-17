"""抓 adb logcat 并按关键词过滤，避免 PowerShell 编码/转义问题。

用法: python adblog.py <关键词1,关键词2,...> [最大条数] [--clear]
示例: python adblog.py 副屏,ScreenModule,agent 40
"""
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ADB = r'C:\Users\Administrator\devtools\android-sdk\platform-tools\adb.exe'

args = [a for a in sys.argv[1:] if not a.startswith('--')]
clear = '--clear' in sys.argv
keywords = args[0].split(',') if args else []
limit = int(args[1]) if len(args) > 1 else 40

if clear:
    subprocess.run([ADB, 'logcat', '-c'], capture_output=True)

out = subprocess.run([ADB, 'logcat', '-d', '-v', 'brief'],
                     capture_output=True, text=True, encoding='utf-8', errors='replace')
lines = out.stdout.splitlines()
hits = [l for l in lines if any(k in l for k in keywords)] if keywords else lines
for l in hits[-limit:]:
    print(l)
print('--- 命中 %d / 总 %d 行 ---' % (len(hits), len(lines)))
