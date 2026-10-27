#!/usr/bin/env python3
"""Fail when the English/Vietnamese catalogs drift or contain empty strings."""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
packs = {}
for code in ("en", "vi"):
    path = root / "languages" / f"{code}.langhc"
    with path.open(encoding="utf-8") as stream:
        pack = json.load(stream)
    packs[code] = pack["translations"]

english, vietnamese = packs["en"], packs["vi"]
missing_vi = sorted(set(english) - set(vietnamese))
missing_en = sorted(set(vietnamese) - set(english))
empty = sorted((code, key) for code, values in packs.items()
                for key, value in values.items() if not value.strip())
source_keys = set()
unlocalized_ui = []
vietnamese_letters = "ăâđêôơưĂÂĐÊÔƠƯàáạảãằắặẳẵầấậẩẫèéẹẻẽềếệểễìíịỉĩòóọỏõồốộổỗờớợởỡùúụủũừứựửữỳýỵỷỹ"
for source in (root / "src").rglob("*"):
    if source.suffix not in {".cpp", ".h"}:
        continue
    contents = source.read_text(encoding="utf-8", errors="replace")
    source_keys.update(re.findall(r'\bLTR\s*\(\s*"([^"]+)"', contents))
    if "src/ui" in source.as_posix():
        for match in re.finditer(r'\btr\s*\(\s*(?:"(?:\\.|[^"\\])*"\s*)+\)', contents, re.S):
            if any(letter in match.group() for letter in vietnamese_letters):
                line = contents.count("\n", 0, match.start()) + 1
                unlocalized_ui.append(f"{source.relative_to(root)}:{line}")
missing_source = sorted(source_keys - set(english))
if missing_vi or missing_en or empty or missing_source or unlocalized_ui:
    if missing_vi:
        print("Missing Vietnamese keys:", *missing_vi, sep="\n  ", file=sys.stderr)
    if missing_en:
        print("Missing English keys:", *missing_en, sep="\n  ", file=sys.stderr)
    if empty:
        print("Empty translations:", *empty, sep="\n  ", file=sys.stderr)
    if missing_source:
        print("Literal LTR() keys missing from both catalogs:", *missing_source,
              sep="\n  ", file=sys.stderr)
    if unlocalized_ui:
        print("Vietnamese UI text bypasses the language packs:", *unlocalized_ui,
              sep="\n  ", file=sys.stderr)
    raise SystemExit(1)

print(f"Vietnamese/English parity: 100% ({len(english)} matching keys; no empty strings; "
      f"all {len(source_keys)} literal LTR() keys translated)")
