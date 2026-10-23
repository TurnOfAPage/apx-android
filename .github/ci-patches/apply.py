#!/usr/bin/env python3
"""AI 编辑通道：按同目录 edits.json 做"精确匹配→替换"。

约束（与 CI 一致）：
- 每条 find 必须在目标文件命中 expect 次（默认 1），命中数不符直接失败、不写盘；
- 两遍执行：第一遍只校验，第二遍替换前再验一次次数（防同文件内前后条目互相干扰）；
- 成功后删除 edits.json，使触发它的那次 push 重入时变成 no-op，杜绝循环。
"""
import json
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]


def fail(msg: str) -> int:
    print(f"::error::{msg}")
    return 1


def main() -> int:
    edits_path = pathlib.Path(__file__).with_name("edits.json")
    if not edits_path.exists():
        print("no edits.json — nothing to do")
        return 0
    edits = json.loads(edits_path.read_text(encoding="utf-8")).get("edits", [])
    if not edits:
        print("edits.json empty — nothing to do")
        return 0

    staged = {}
    for i, e in enumerate(edits, 1):
        path = e["path"]
        find = e["find"]
        repl = e["replace"]
        expect = int(e.get("expect", 1))
        if not find:
            return fail(f"#{i}: empty find")
        target = REPO / path
        if not target.is_file():
            return fail(f"#{i}: missing file {path}")
        text = target.read_text(encoding="utf-8")
        n = text.count(find)
        if n != expect:
            return fail(f"#{i}: {path} — find hit {n}, expected {expect}\nfind head: {find[:160]!r}")
        staged.setdefault(path, []).append((find, repl, expect))

    for path, items in staged.items():
        target = REPO / path
        text = target.read_text(encoding="utf-8")
        for find, repl, expect in items:
            n = text.count(find)
            if n != expect:
                return fail(f"{path} — recheck hit {n}, expected {expect} (in-file interference)")
            text = text.replace(find, repl)
        target.write_text(text, encoding="utf-8")
        print(f"OK {path}: {len(items)} edit(s)")

    edits_path.unlink()
    print("edits.json consumed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
