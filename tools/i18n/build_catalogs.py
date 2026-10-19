"""Build the desktop app's language catalogs, i18n/<lang>.json, which are copied beside the executable.

Each is the web version's catalog (tools/i18n/web/<lang>.json, copied from bitzkey_daemon/web/i18n,
where the shared text is translated) with the desktop's own text added: the keys in
tools/i18n/desktop_keys.json, translated in tools/i18n/desktop/<lang>.json. Every translation must
keep its key's blanks, {#} for numbers and {*} for words, though it may number them to reorder them.

    python tools/i18n/build_catalogs.py [--missing]
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent.parent / "i18n"
LANGUAGES = ["fr", "it", "es", "de", "pt", "pl", "uk", "zh", "ja", "hi", "he"]
BLANK = re.compile(r"\{([#*])\d*\}")


def blanks(text):
    found = BLANK.findall(text)
    return found.count("#"), found.count("*")


def main():
    keys = json.load(open(HERE / "desktop_keys.json", encoding="utf8"))
    known = set(keys)
    OUT.mkdir(exist_ok=True)
    problems = 0
    for lang in LANGUAGES:
        catalog = json.load(open(HERE / "web" / f"{lang}.json", encoding="utf8"))
        own_path = HERE / "desktop" / f"{lang}.json"
        own = json.load(open(own_path, encoding="utf8")) if own_path.exists() else {}
        bad = [key for key, text in own.items() if not text or blanks(text) != blanks(key)]
        stale = [key for key in own if key not in known]
        missing = [key for key in keys if key not in own]
        for key in keys:
            if key in own and key not in bad:
                catalog[key] = own[key]
        json.dump(catalog, open(OUT / f"{lang}.json", "w", encoding="utf8", newline="\n"), ensure_ascii=False, indent=0)
        print(f"{lang}: {len(catalog)} in all, desktop {len(keys) - len(missing)}/{len(keys)}  "
              f"missing {len(missing)}  bad {len(bad)}  stale {len(stale)}")
        for key in bad:
            print(f"   bad blanks: {key!r} -> {own[key]!r}")
        for key in stale:
            print(f"   stale: {key!r}")
        if "--missing" in sys.argv:
            for key in missing:
                print(f"   missing: {key}")
        problems += len(bad) + len(missing)
    return 1 if problems else 0


sys.exit(main())
