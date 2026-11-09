#!/usr/bin/env python3
"""Build resources/gamedb/<slug>.tsv from libretro-database.

Usage: gamedb.py <libretro-database commit> <output.tsv>
The output's file name picks the console: snes.tsv, gba.tsv or gbc.tsv.
"""
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://raw.githubusercontent.com/libretro/libretro-database/{commit}/metadat/{folder}/{file}"
DATS = {
    "snes": "Nintendo - Super Nintendo Entertainment System.dat",
    "gba": "Nintendo - Game Boy Advance.dat",
    "gbc": "Nintendo - Game Boy Color.dat",
}

# (folder, DAT field, output column)
SOURCES = [
    ("genre", "genre", "genre"),
    ("maxusers", "users", "players"),
    ("developer", "developer", "developer"),
    ("publisher", "publisher", "publisher"),
    ("releaseyear", "releaseyear", "year"),
    ("franchise", "franchise", "franchise"),
]
COLUMNS = ["name", "genre", "players", "developer", "publisher", "year", "franchise"]

GAME_RE = re.compile(r"^game \(\n(.*?)^\)", re.M | re.S)
ROM_CRC_RE = re.compile(r"\brom \(.*?\bcrc ([0-9A-Fa-f]{8})\b", re.S)


def fetch(commit, folder, dat):
    url = BASE.format(commit=commit, folder=folder, file=urllib.parse.quote(dat))
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            return resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        # Smaller systems lack some of the optional metadata folders.
        if e.code == 404 and folder != "no-intro":
            return ""
        raise


def field(block, key):
    m = re.search(r'^\t%s (?:"([^"]*)"|(\S+))' % re.escape(key), block, re.M)
    if not m:
        return ""
    return (m.group(1) if m.group(1) is not None else m.group(2))


def clean(value):
    return re.sub(r"[\t\r\n]", " ", value).strip()


def parse(text, key):
    """Yield (crc, value) for every rom of every game block."""
    for m in GAME_RE.finditer(text.replace("\r\n", "\n")):
        block = m.group(1)
        value = field(block, key)
        for crc in ROM_CRC_RE.findall(block):
            yield crc.lower(), clean(value)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    commit, out = sys.argv[1], sys.argv[2]
    slug = os.path.splitext(os.path.basename(out))[0]
    if slug not in DATS:
        sys.exit("unknown console: %s" % slug)
    dat = DATS[slug]

    rows = {}
    for crc, name in parse(fetch(commit, "no-intro", dat), "name"):
        if name:
            rows.setdefault(crc, dict.fromkeys(COLUMNS, ""))["name"] = name

    for folder, key, column in SOURCES:
        for crc, value in parse(fetch(commit, folder, dat), key):
            if crc in rows and value:
                rows[crc][column] = value

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("#equinox-gamedb\t1\t%s\n" % commit)
        for crc in sorted(rows):
            f.write("\t".join([crc] + [rows[crc][c] for c in COLUMNS]) + "\n")


if __name__ == "__main__":
    main()
