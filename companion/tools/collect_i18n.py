"""Findet deutsche Oberflächentexte in web/*.html, die in web/i18n.js noch keine Übersetzung haben.

Aufruf: python tools/collect_i18n.py
Gibt fehlende Sätze aus und endet mit Exit-Code 1, wenn welche fehlen (build.bat bricht dann ab).
Erkannt werden Textknoten, die Attribute title/placeholder/aria-label und Zeichenketten im Skript.
Werte mitten im Satz stehen im Code als t("… {count} …", {...}) und werden so ebenfalls gefunden.
Alles unter data-raw (Namen, Titel, Pfade) bleibt unübersetzt und wird übersprungen.
"""
import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

WEB = Path(__file__).resolve().parent.parent / "web"
ATTRS = {"title", "placeholder", "aria-label"}
LETTERS = re.compile(r"[A-Za-zÄÖÜäöüß]{2,}")
# Code-Bezeichner, Klassen, Pfade, Adressen – keine Oberflächentexte
CODE_LIKE = re.compile(r"^[\w.#:/\\\[\]=*>+~@$%()-]+$|^https?://|^[a-z]+(_[a-z]+)+$")
STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"|`((?:[^`\\]|\\.)*)`')
# Ohne Leerzeichen zählt nur ein großgeschriebenes deutsches Wort als Text („Speichern“), nicht „play_pause“
PROSE = re.compile(r"[ ÄÖÜäöüß…–]|^[A-ZÄÖÜ][a-zäöüß]+$")


def load_table() -> dict:
    src = (WEB / "i18n.js").read_text(encoding="utf-8")
    body = src.split("/*EN-START*/", 1)[1].split("/*EN-END*/", 1)[0].strip().rstrip(",")
    return json.loads("{" + body + "}")


class Collector(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.stack: list[tuple[str, bool]] = []
        self.raw_depth = 0
        self.found: set[str] = set()
        self.scripts: list[str] = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        raw = "data-raw" in a or "data-i18n-html" in a  # Blöcke sammelt html_blocks() als Ganzes
        if tag not in ("meta", "link", "input", "img", "br", "hr"):
            self.stack.append((tag, raw))
            self.raw_depth += raw
        if not self.raw_depth and not raw:
            for k, v in a.items():
                if k in ATTRS and v and LETTERS.search(v) and not CODE_LIKE.search(v) and not re.match(r"^[A-Z]:\\", v):
                    self.found.add(v.strip())

    def handle_endtag(self, tag):
        while self.stack:
            t, raw = self.stack.pop()
            self.raw_depth -= raw
            if t == tag:
                break

    def handle_data(self, data):
        tag = self.stack[-1][0] if self.stack else ""
        if tag == "script":
            self.scripts.append(data)
            return
        if tag == "style" or self.raw_depth:
            return
        text = " ".join(data.split())
        if text and LETTERS.search(text):
            self.found.add(text)


def script_strings(code: str) -> set[str]:
    out = set()
    for m in STRING.finditer(code):
        s = m.group(1) if m.group(1) is not None else m.group(2)
        s = s.strip()
        # Klassenlisten wie "toast show" und Pfade in Beispielen sind keine Oberflächentexte
        if re.fullmatch(r"[a-z-]+( [a-z-]+)*", s) or re.match(r"^[A-Z]:\\", s):
            continue
        if "${" in s or "<" in s or not LETTERS.search(s) or CODE_LIKE.search(s) or not PROSE.search(s):
            continue
        out.add(s)
    return out


BLOCK = re.compile(r"<(\w+)\b[^>]*\bdata-i18n-html\b[^>]*>(.*?)</\1>", re.S)


def html_blocks(src: str) -> set[str]:
    """Inhalt der data-i18n-html-Elemente, Leerraum zusammengefasst wie in i18n.js."""
    return {" ".join(m.group(2).split()) for m in BLOCK.finditer(src)}


def collect() -> set[str]:
    texts: set[str] = set()
    for page in sorted(WEB.glob("*.html")):
        src = page.read_text(encoding="utf-8")
        c = Collector()
        c.feed(src)
        texts |= c.found | html_blocks(src)
        for code in c.scripts:
            texts |= script_strings(code)
    return texts


def main() -> int:
    table = load_table()
    missing = sorted(t for t in collect() if t not in table)
    for t in missing:
        print(json.dumps(t, ensure_ascii=False) + ": \"\",")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
