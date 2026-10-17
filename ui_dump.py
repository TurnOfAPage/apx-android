"""解析 uiautomator dump 的 ui.xml，列出可观测控件（开关/可点击项）。

用法: python ui_dump.py [ui.xml] [--all]
"""
import re
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

path = sys.argv[1] if len(sys.argv) > 1 else 'ui.xml'
show_all = '--all' in sys.argv

s = open(path, encoding='utf-8').read()
nodes = re.findall(r'<node[^>]*>', s)


def attr(n, key):
    m = re.search(key + r'="([^"]*)"', n)
    return m.group(1) if m else ''


def center(bounds):
    m = re.match(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', bounds)
    if not m:
        return None
    x1, y1, x2, y2 = map(int, m.groups())
    return (x1 + x2) // 2, (y1 + y2) // 2


print('节点总数 %d' % len(nodes))
print('%-30s %-34s %-8s %-22s %s' % ('text/desc', 'resource-id', 'checked', 'bounds', 'center'))
print('-' * 120)
for n in nodes:
    checked = attr(n, 'checked')
    text = attr(n, 'text') or attr(n, 'content-desc')
    rid = attr(n, 'resource-id')
    bounds = attr(n, 'bounds')
    clickable = attr(n, 'clickable') == 'true'
    if not checked and not (show_all and clickable):
        continue
    if not text and not rid:
        continue
    c = center(bounds)
    print('%-30s %-34s %-8s %-22s %s' % (
        text[:30], rid.replace('com.allperiph:id/', '')[:34],
        checked or '-', bounds, c or ''))
