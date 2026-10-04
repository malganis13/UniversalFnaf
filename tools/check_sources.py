"""Structural sanity check for the UniversalFnaf sources.

Not a compiler. It strips comments and string/char literals, then verifies that
braces / parentheses / brackets are balanced and that no TODO-style placeholder
survived into the deliverable. Run it after any bulk edit:

    python tools/check_sources.py
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

PAIRS = {")": "(", "]": "[", "}": "{"}


def strip_code(text: str) -> str:
    """Remove comments and string/char literal contents."""
    out: list[str] = []
    i = 0
    n = len(text)
    state = "code"  # code | line | block | str | chr
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line"; i += 2; continue
            if ch == "/" and nxt == "*":
                state = "block"; i += 2; continue
            if ch == '"':
                state = "str"; i += 1; continue
            if ch == "'":
                state = "chr"; i += 1; continue
            out.append(ch); i += 1; continue
        if state == "line":
            if ch == "\n":
                state = "code"; out.append(ch)
            i += 1; continue
        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"; i += 2; continue
            i += 1; continue
        # str / chr
        if ch == "\\":
            i += 2; continue
        if (state == "str" and ch == '"') or (state == "chr" and ch == "'"):
            state = "code"
        i += 1
    return "".join(out)


def check(path: pathlib.Path) -> list[str]:
    problems: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_code(text)

    stack: list[tuple[str, int]] = []
    line = 1
    for ch in code:
        if ch == "\n":
            line += 1
        elif ch in "([{":
            stack.append((ch, line))
        elif ch in ")]}":
            if not stack:
                problems.append(f"{path.name}: unmatched '{ch}' at line {line}")
            else:
                opener, oline = stack.pop()
                if opener != PAIRS[ch]:
                    problems.append(
                        f"{path.name}: '{opener}' (line {oline}) closed by '{ch}' at line {line}"
                    )
    for opener, oline in stack:
        problems.append(f"{path.name}: '{opener}' opened at line {oline} is never closed")

    for number, raw in enumerate(text.splitlines(), start=1):
        if re.search(r"\b(TODO|FIXME|XXX|HACK)\b", raw):
            problems.append(f"{path.name}: placeholder marker on line {number}: {raw.strip()}")

    return problems


def main() -> int:
    files = sorted(p for p in (ROOT / "src").rglob("*") if p.suffix == ".cpp")
    files += sorted(p for p in (ROOT / "include").rglob("*") if p.suffix == ".h")

    problems: list[str] = []
    for path in files:
        problems.extend(check(path))

    print(f"checked {len(files)} files")
    for problem in problems:
        print("  " + problem)
    if problems:
        return 1
    print("no structural problems found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
