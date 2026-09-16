"""Brücke zwischen Spotify, dem Gerät und der PC-App.

Liest die Wiedergabe über die Windows-Mediensteuerung (SMTC) und, wenn
angemeldet, zusätzlich über die Spotify-Web-API (echte Künstlerliste, Like,
Smart Shuffle, Spotify-eigene Lautstärke, Playlists). Ohne Anmeldung regelt
sie die Lautstärke über Spotifys Sitzung im Windows-Lautstärkemixer.

Liefert die Seiten der PC-App unter http://127.0.0.1:8765 und spricht mit dem
Gerät (jetzt der Simulator per TCP, später USB oder Bluetooth). Normalerweise
startet deck_thing.py die Brücke im selben Prozess; allein geht es zum Entwickeln:

Start:  python bridge.py --no-browser
"""

from __future__ import annotations

import asyncio
import hashlib
import html
import io
import json
import ctypes
import logging
import os
import re
import shlex
import subprocess
import sys
import threading
import time
import webbrowser
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

import aiohttp
from aiohttp import WSMsgType, web
from PIL import Image, ImageDraw, ImageFont, ImageOps
from winrt.windows.media.control import (
    GlobalSystemMediaTransportControlsSessionManager as SessionManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
)
from winrt.windows.storage.streams import Buffer, InputStreamOptions

import i18n
from audio_mixer import AudioMixer
from i18n import t
from lvgl_image import rgb565a8
import paths
from paths import DATA_DIR
from spotify_api import REDIRECT_URI, SpotifyAPI, SpotifyError

try:  # Enum für Wiederholen liegt im Paket winrt-Windows.Media
    from winrt.windows.media import MediaPlaybackAutoRepeatMode as RepeatMode
except ImportError:  # pragma: no cover
    RepeatMode = None

HERE = Path(__file__).resolve().parent
# Seiten und Schriften: neben dem Skript, in der gebauten App im PyInstaller-Ordner
RES = Path(getattr(sys, "_MEIPASS", HERE))
HOST, PORT = "127.0.0.1", 8765
POLL_SECONDS = 0.5
FAST_POLL_SECONDS = 0.1   # kurz nach Skip/Play am Gerät: neuer Titel und Cover sollen sofort erscheinen
FAST_POLL_WINDOW = 3.0
COVER_RETRY_SECONDS = 3.0  # Spotify liefert das neue Cover oft erst kurz nach dem Titel
COVER_RETRY_EVERY = 0.3
API_POLL_SECONDS = 2.0
VOLUME_DEBOUNCE = 0.25
VOLUME_HOLD_SECONDS = 8.0  # nach dem Verstellen den eigenen Wert melden, bis Spotify ihn bestätigt (höchstens so lange)
COVER_SIZE = 240

# SMTC: NONE=0, TRACK=1, LIST=2   Oberfläche: 0 aus, 1 Playlist, 2 Titel
SMTC_TO_UI_REPEAT = {0: 0, 2: 1, 1: 2}
UI_TO_SMTC_REPEAT = {v: k for k, v in SMTC_TO_UI_REPEAT.items()}

log = logging.getLogger("bridge")


class SpotifyVolume:
    """Lautstärke nur von Spotify, über dessen Sitzung im Windows-Mixer.

    pycaw spricht COM, deshalb läuft alles in einem eigenen Thread mit
    CoInitialize. Spotify startet mehrere Prozesse (Desktop- und Store-Version
    heißen beide Spotify.exe); wir nehmen jede Sitzung, deren Prozessname mit
    "spotify" beginnt.
    """

    def __init__(self) -> None:
        self._pool = ThreadPoolExecutor(max_workers=1, initializer=self._init_com)

    @staticmethod
    def _init_com() -> None:
        import comtypes

        comtypes.CoInitialize()

    @staticmethod
    def _volumes():
        from pycaw.pycaw import AudioUtilities

        found = []
        for session in AudioUtilities.GetAllSessions():
            proc = session.Process
            if proc is None:
                continue
            try:
                name = proc.name().lower()
            except Exception:
                continue
            if name.startswith("spotify"):
                found.append(session.SimpleAudioVolume)
        return found

    def _get(self):
        vols = self._volumes()
        if not vols:
            return None
        return round(vols[0].GetMasterVolume() * 100), bool(vols[0].GetMute())

    def _set(self, percent: int) -> None:
        for vol in self._volumes():
            vol.SetMasterVolume(max(0, min(100, percent)) / 100, None)
            vol.SetMute(0, None)

    def _mute(self, muted: bool) -> None:
        for vol in self._volumes():
            vol.SetMute(1 if muted else 0, None)

    async def _run(self, fn, *args):
        return await asyncio.get_running_loop().run_in_executor(self._pool, fn, *args)

    async def get(self):
        return await self._run(self._get)

    async def set(self, percent: int) -> None:
        await self._run(self._set, percent)

    async def mute(self, muted: bool) -> None:
        await self._run(self._mute, muted)


async def read_thumbnail(ref) -> bytes:
    stream = await ref.open_read_async()
    size = int(stream.size)
    buf = Buffer(size)
    await stream.read_async(buf, size, InputStreamOptions.READ_AHEAD)
    return bytes(memoryview(buf))


# ---------- Tastenseite (Stream-Deck-Modus) ----------
KEYS_FILE = DATA_DIR / "keys.json"
DEFAULT_KEYS = {
    "pages": [{
        "name": "Werkzeuge",
        "keys": [
            {"label": "F13", "icon": "keyboard", "color": "#E0B25C", "action": {"type": "key", "keys": "f13"}},
            {"label": "Screenshot", "icon": "scan", "color": "#8EC5E8", "action": {"type": "key", "keys": "win+shift+s"}},
            {"label": "Ton aus", "icon": "volume-x", "color": "#E07A5F", "action": {"type": "key", "keys": "volumemute"}},
            {"label": "Rechner", "icon": "calculator", "color": "#B4D98E", "action": {"type": "launch", "path": "calc.exe"}},
            {"label": "Explorer", "icon": "folder", "color": "#E0B25C", "action": {"type": "launch", "path": "explorer.exe"}},
            {"label": "Browser", "icon": "globe", "color": "#8EC5E8", "action": {"type": "url", "url": "https://www.google.com"}},
            {"label": "Task-Manager", "icon": "activity", "color": "#C9A7EB", "action": {"type": "key", "keys": "ctrl+shift+esc"}},
            {"label": "Desktop", "icon": "monitor", "color": "#B4D98E", "action": {"type": "key", "keys": "win+d"}},
        ],
    }],
}

# virtuelle Tastencodes für Kombinationen wie "ctrl+shift+m"; Buchstaben, Ziffern und F1–F24 werden berechnet
VIRTUAL_KEYS = {
    "ctrl": 0x11, "shift": 0x10, "alt": 0x12, "win": 0x5B,
    "esc": 0x1B, "enter": 0x0D, "tab": 0x09, "space": 0x20, "backspace": 0x08, "delete": 0x2E,
    "home": 0x24, "end": 0x23, "pageup": 0x21, "pagedown": 0x22,
    "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28, "printscreen": 0x2C,
    "volumemute": 0xAD, "volumedown": 0xAE, "volumeup": 0xAF,
    "medianext": 0xB0, "mediaprev": 0xB1, "mediastop": 0xB2, "mediaplaypause": 0xB3,
}


KEY_COLUMNS = 4          # sichtbar sind 4 × 2 Tasten
MAX_KEYS = 48
SCROLL_MODES = {"horizontal": 8, "vertical": 4}  # weitere Tasten: ganze Seiten nach rechts oder Reihen nach unten
# Symbole, die das Gerät kennt (muss zur Tabelle ICONS in firmware/ui/ui_deck.c passen)
KEY_ICONS = [
    "keyboard", "mic-off", "mic", "scan", "camera", "calculator", "folder", "globe", "activity", "monitor",
    "app-window", "lock", "power", "settings", "zap", "command", "terminal", "music", "volume-2", "volume-x",
    "play", "skip-forward", "plus", "message-circle", "gamepad-2", "video", "house", "star",
]
ACTION_TYPES = {"key", "launch", "script", "url", "media"}
SYMBOL_TYPES = {"icon", "emoji", "image"}
ICON_DIR = KEYS_FILE.parent / "icons"  # hochgeladene eigene Bilder, als PNG unter ihrer Prüfsumme
IMAGE_NAME_RE = re.compile(r"[0-9a-f]{16}\.png")
EMOJI_FONT = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "seguiemj.ttf"
KEY_ICON_SIZE = 56  # Pixel auf dem Gerät, etwas größer als die 48er Lucide-Zeichen
KEY_SIZE = (172, 150)  # ganze Taste auf dem Gerät (Bild füllt die Taste)
KEY_RADIUS = 16
IMAGE_SHAPES = {"square", "rounded", "circle"}
MAX_ICON_UPLOAD = 8 * 1024 * 1024
key_icons: dict[str, bytes] = {}  # Kennung → Breite | Höhe | RGB565A8, fertig für den Rahmen
MEDIA_COMMANDS = {"play_pause", "next", "prev", "volumeup", "volumedown", "volumemute"}


def load_keys() -> dict:
    """Belegung lesen; beim ersten Start die Standardbelegung anlegen. Leere Felder sind None; die Anzahl
    wird auf ganze Seiten (nach rechts) bzw. ganze Reihen (nach unten) aufgefüllt."""
    if not KEYS_FILE.exists():
        save_keys(DEFAULT_KEYS)
    try:
        data = json.loads(KEYS_FILE.read_text(encoding="utf-8"))
        if isinstance(data.get("pages"), list) and data["pages"]:
            for page in data["pages"]:
                if page.get("scroll") not in SCROLL_MODES:
                    page["scroll"] = "horizontal"
                unit = SCROLL_MODES[page["scroll"]]
                keys = list(page.get("keys") or [])[:MAX_KEYS]
                size = max(8, -(-len(keys) // unit) * unit)
                page["keys"] = keys + [None] * (size - len(keys))
            return data
    except (OSError, ValueError) as err:
        log.warning("Tastenbelegung %s nicht lesbar (%s), nehme die Standardbelegung", KEYS_FILE, err)
    return json.loads(json.dumps(DEFAULT_KEYS))


def save_keys(data: dict) -> None:
    KEYS_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = KEYS_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(KEYS_FILE)  # nie eine halb geschriebene Belegung hinterlassen


def validate_keys(data: dict) -> str | None:
    """Fehlertext für den Editor oder None, wenn die Belegung in Ordnung ist."""
    pages = data.get("pages") if isinstance(data, dict) else None
    if not isinstance(pages, list) or not pages:
        return "Keine Seite in der Belegung"
    for page in pages:
        keys = page.get("keys")
        if page.get("scroll", "horizontal") not in SCROLL_MODES:
            return "Unbekannte Richtung für weitere Tasten"
        if not isinstance(keys, list) or len(keys) > MAX_KEYS:
            return f"Höchstens {MAX_KEYS} Tasten"
        if len(keys) % SCROLL_MODES[page.get("scroll", "horizontal")]:
            return "Tastenanzahl passt nicht zu ganzen Seiten bzw. Reihen"
        for number, key in enumerate(keys, start=1):
            if key is None:
                continue
            if not str(key.get("label", "")).strip():
                return f"Taste {number} braucht einen Namen"
            symbol = key.get("symbol", "icon")
            if symbol not in SYMBOL_TYPES:
                return f"Taste {number}: unbekannte Symbolart"
            if symbol == "icon" and key.get("icon") not in KEY_ICONS:
                return f"Taste {number}: unbekanntes Symbol"
            if symbol == "emoji":
                try:
                    emoji_image(str(key.get("emoji", "")))
                except ValueError as err:
                    return f"Taste {number}: {err}"
            if symbol == "image":
                name = str(key.get("image", ""))
                if not IMAGE_NAME_RE.fullmatch(name) or not (ICON_DIR / name).exists():
                    return f"Taste {number}: Bild fehlt, bitte neu auswählen"
                if key.get("image_shape", "square") not in IMAGE_SHAPES:
                    return f"Taste {number}: unbekannte Bildform"
            for field in ("color", "text_color"):
                if not re.fullmatch(r"#[0-9a-fA-F]{6}", str(key.get(field, "#FFFFFF"))):
                    return f"Taste {number}: Farbe muss #RRGGBB sein"
            action = key.get("action") or {}
            if action.get("type") not in ACTION_TYPES:
                return f"Taste {number}: unbekannte Aktion"
    return None


# Zeichen, aus denen ein einzelnes Emoji bestehen darf (inkl. Hautfarbe, Verbinder, Tastenkappen wie 1️⃣)
EMOJI_PART_RANGES = (
    (0x1F000, 0x1FAFF), (0x2600, 0x27BF), (0x2B00, 0x2BFF), (0x2300, 0x23FF), (0x2190, 0x21FF), (0x25A0, 0x25FF),
    (0xFE0F, 0xFE0F), (0x200D, 0x200D), (0x20E3, 0x20E3), (0xE0020, 0xE007F),
    (0x00A9, 0x00A9), (0x00AE, 0x00AE), (0x203C, 0x203C), (0x2049, 0x2049), (0x2122, 0x2122), (0x2139, 0x2139),
    (0x3030, 0x3030), (0x303D, 0x303D), (0x3297, 0x3297), (0x3299, 0x3299),
)


def is_emoji_part(ch: str) -> bool:
    return ch in "#*0123456789" or any(lo <= ord(ch) <= hi for lo, hi in EMOJI_PART_RANGES)


def emoji_image(text: str) -> Image.Image:
    """Emoji in Farbe über Windows' Segoe UI Emoji, knapp zugeschnitten und freigestellt."""
    text = text.strip()
    if not text:
        raise ValueError(t("Emoji fehlt"))
    if len(text) > 16 or not all(is_emoji_part(ch) for ch in text) or all(ch in "#*0123456789" for ch in text):
        raise ValueError(t("bitte genau ein Emoji, keine Buchstaben"))
    font = ImageFont.truetype(str(EMOJI_FONT), 109)
    canvas = Image.new("RGBA", (320, 200), (0, 0, 0, 0))
    ImageDraw.Draw(canvas).text((160, 100), text, font=font, anchor="mm", embedded_color=True)
    box = canvas.getchannel("A").getbbox()
    if box is None:
        raise ValueError(t("dieses Zeichen kann Windows nicht darstellen"))
    return canvas.crop(box)


def fit_icon(img: Image.Image, size: int = KEY_ICON_SIZE) -> Image.Image:
    img = img.convert("RGBA")
    img.thumbnail((size, size), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(img, ((size - img.width) // 2, (size - img.height) // 2))
    return out


def key_icon_id(key: dict) -> str | None:
    """Kennung für Emoji- und Bildsymbole; Lucide-Zeichen hat das Gerät selbst."""
    symbol = key.get("symbol", "icon")
    if symbol == "emoji":
        source = "emoji:" + str(key.get("emoji", "")).strip()
    elif symbol == "image":
        source = f"image:{key.get('image', '')}:{key.get('image_shape', 'square')}:{bool(key.get('image_fill'))}"
    else:
        return None
    return hashlib.sha1(f"{source}#{KEY_ICON_SIZE}".encode("utf-8")).hexdigest()[:16]


def shape_mask(size: tuple[int, int], shape: str, radius: int) -> Image.Image:
    """Maske mit weichen Kanten (vierfach gezeichnet, dann verkleinert)."""
    big = Image.new("L", (size[0] * 4, size[1] * 4), 0)
    draw = ImageDraw.Draw(big)
    box = (0, 0, big.width - 1, big.height - 1)
    if shape == "circle":
        draw.ellipse(box, fill=255)
    else:
        draw.rounded_rectangle(box, radius=radius * 4, fill=255)
    return big.resize(size, Image.Resampling.LANCZOS)


def key_image(key: dict) -> Image.Image:
    """Eigenes Bild: als Symbol (optional abgerundet/rund) oder über die ganze Taste mit Schatten für die Schrift."""
    img = Image.open(ICON_DIR / str(key.get("image", ""))).convert("RGBA")
    if key.get("image_fill"):
        img = ImageOps.fit(img, KEY_SIZE, Image.Resampling.LANCZOS)
        if str(key.get("label", "")).strip():
            shade = Image.new("L", (1, KEY_SIZE[1]))
            for y in range(KEY_SIZE[1]):  # untere Hälfte nach unten hin abdunkeln, damit die Beschriftung lesbar bleibt
                shade.putpixel((0, y), int(170 * max(0.0, (y / KEY_SIZE[1] - 0.45) / 0.55)))
            black = Image.new("RGBA", KEY_SIZE, (0, 0, 0, 255))
            black.putalpha(shade.resize(KEY_SIZE))
            img = Image.alpha_composite(img, black)
        alpha = Image.composite(img.getchannel("A"), Image.new("L", KEY_SIZE, 0), shape_mask(KEY_SIZE, "rounded", KEY_RADIUS))
        img.putalpha(alpha)
        return img
    shape = key.get("image_shape", "square")
    if shape == "square":
        return fit_icon(img)
    side = KEY_ICON_SIZE
    img = ImageOps.fit(img, (side, side), Image.Resampling.LANCZOS)
    alpha = Image.composite(img.getchannel("A"), Image.new("L", (side, side), 0), shape_mask((side, side), shape, 12))
    img.putalpha(alpha)
    return img


def prepare_key_icon(key: dict) -> str | None:
    """Symbolbild rechnen (einmal je Kennung) und die Kennung liefern; None = Lucide-Zeichen nehmen."""
    icon_id = key_icon_id(key)
    if icon_id is None or icon_id in key_icons:
        return icon_id
    try:
        if key.get("symbol") == "emoji":
            img = fit_icon(emoji_image(str(key.get("emoji", ""))))
        else:
            img = key_image(key)
    except Exception as err:
        log.warning("Tastensymbol „%s“ nicht darstellbar: %s", key.get("label"), err)
        return None
    key_icons[icon_id] = img.width.to_bytes(2, "little") + img.height.to_bytes(2, "little") + rgb565a8(img)
    return icon_id


def device_keys_message() -> dict:
    """Belegung fürs Gerät: nur Beschriftung, Symbol und Farben; leere Felder bleiben leer, die Aktionen am PC."""
    pages = []
    for page in load_keys()["pages"]:
        keys = []
        for key in page["keys"]:
            if key is None:
                keys.append({"empty": True, "label": "", "icon": "", "color": "#000000"})
                continue
            entry = {"label": device_text(key.get("label")), "icon": key.get("icon", "keyboard"),
                     "color": key.get("color", "#FFFFFF"), "text_color": key.get("text_color", "#FFFFFF")}
            icon_id = prepare_key_icon(key)
            if icon_id:
                entry["icon_id"] = icon_id
                entry["icon_fill"] = key.get("symbol") == "image" and bool(key.get("image_fill"))
            keys.append(entry)
        pages.append({"name": device_text(page.get("name")), "scroll": page["scroll"], "keys": keys})
    return {"type": "keys", "pages": pages}


def send_hotkey(combo: str) -> None:
    codes = []
    for part in combo.lower().replace(" ", "").split("+"):
        if part in VIRTUAL_KEYS:
            codes.append(VIRTUAL_KEYS[part])
        elif len(part) == 1 and part.isalnum():
            codes.append(ord(part.upper()))
        elif part.startswith("f") and part[1:].isdigit() and 1 <= int(part[1:]) <= 24:
            codes.append(0x6F + int(part[1:]))  # F1 = 0x70
        else:
            raise ValueError(t("unbekannte Taste „{part}“", part=part))
    user32 = ctypes.windll.user32
    # Mit Scancode schicken: manche Programme (Spiele, Discord-Tastenkürzel) werten nur den aus, nicht den VK-Code
    extended = {0x5B, 0x2E, 0x24, 0x23, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3}
    for vk in codes:
        user32.keybd_event(vk, user32.MapVirtualKeyW(vk, 0), 1 if vk in extended else 0, 0)
    for vk in reversed(codes):
        user32.keybd_event(vk, user32.MapVirtualKeyW(vk, 0), (1 if vk in extended else 0) | 2, 0)  # 2 = KEYEVENTF_KEYUP


def run_action(action: dict) -> None:
    """Aktion einer Taste am PC ausführen (Medienbefehle laufen getrennt über die Mediensteuerung)."""
    kind = action.get("type")
    if kind == "key":
        if not action.get("keys"):
            raise ValueError(t("keine Tastenkombination eingetragen"))
        send_hotkey(action["keys"])
    elif kind == "launch":
        path = (action.get("path") or "").strip().strip('"')
        if not path:
            raise ValueError(t("kein Programm eingetragen"))
        args = shlex.split(action.get("args") or "", posix=False)
        if args:
            subprocess.Popen([path, *args], close_fds=True)
        else:
            os.startfile(path)  # findet auch Programme ohne Pfad (calc.exe) und öffnet Dateien mit ihrer App
    elif kind == "script":
        path = (action.get("path") or "").strip().strip('"')
        if not os.path.isfile(path):
            raise ValueError(t("Skript nicht gefunden: {path}", path=path))
        suffix = Path(path).suffix.lower()
        if suffix == ".ps1":
            subprocess.Popen(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path],
                             creationflags=subprocess.CREATE_NO_WINDOW)
        elif suffix == ".py":
            subprocess.Popen([sys.executable, path], creationflags=subprocess.CREATE_NO_WINDOW)
        elif suffix in (".bat", ".cmd"):
            subprocess.Popen(["cmd.exe", "/c", path], creationflags=subprocess.CREATE_NO_WINDOW)
        else:
            raise ValueError(t("Skripte: .ps1, .bat, .cmd oder .py"))
    elif kind == "url":
        url = (action.get("url") or "").strip()
        if not re.match(r"^[a-z][a-z0-9+.-]*:", url, re.IGNORECASE):
            raise ValueError(t("Link muss mit https:// o. Ä. beginnen"))
        webbrowser.open(url)
    else:
        raise ValueError(t("unbekannte Aktion „{kind}“", kind=kind))


EMOJI_RANGES = (
    (0x1F000, 0x1FAFF),  # Emoji, Symbole, Flaggen-Teile
    (0x2600, 0x27BF),    # Wetter, Dingbats
    (0x2B00, 0x2BFF),    # Pfeile und Sterne in Emoji-Darstellung
    (0xFE00, 0xFE0F),    # Varianten-Auswahl (Emoji-Darstellung)
    (0x200D, 0x200D),    # Verbinder zwischen Emoji-Teilen
    (0xE0020, 0xE007F),  # Tag-Zeichen (Flaggen)
)


def device_text(text: str | None) -> str:
    """Emojis entfernen, bevor Text ans Gerät geht: dessen Schrift hat dafür keine Zeichen und
    zeigt sonst leere Kästchen. Buchstaben (auch Umlaute, é, ø) bleiben erhalten."""
    if not text:
        return ""
    kept = "".join(ch for ch in text if not any(lo <= ord(ch) <= hi for lo, hi in EMOJI_RANGES))
    return re.sub(r"\s{2,}", " ", kept).strip()


def spotify_process_running() -> bool:
    """Desktop- und Store-Version laufen beide als Spotify.exe."""
    import psutil

    for proc in psutil.process_iter(["name"]):
        if (proc.info.get("name") or "").lower().startswith("spotify"):
            return True
    return False


FEAT_RE = re.compile(r"\s*[(\[]\s*(?:feat\.?|ft\.?|featuring|with)\s+([^)\]]+)[)\]]", re.IGNORECASE)


def split_featured(title: str, artist: str) -> tuple[str, str]:
    """Spotify meldet an Windows nur den Hauptkünstler; Gäste stehen im Titel.

    "Grünphase (feat. Mira Holt & Jonas Feld)", "Lena Kessler"
    → "Grünphase", "Lena Kessler, Mira Holt, Jonas Feld"  (so zeigt es Spotify selbst)
    """
    title, artist = title or "", artist or ""
    guests: list[str] = []
    for match in FEAT_RE.finditer(title):
        guests += [g.strip() for g in re.split(r",|&| and | und ", match.group(1)) if g.strip()]
    if not guests:
        return title, artist
    names = [a.strip() for a in artist.split(",") if a.strip()]
    names += [g for g in guests if g.lower() not in {n.lower() for n in names}]
    return FEAT_RE.sub("", title).strip(), ", ".join(names)


def square_image(raw: bytes, size: int) -> Image.Image:
    img = Image.open(io.BytesIO(raw)).convert("RGB")
    side = min(img.size)
    left, top = (img.width - side) // 2, (img.height - side) // 2
    return img.crop((left, top, left + side, top + side)).resize((size, size), Image.Resampling.LANCZOS)


def to_jpeg(img: Image.Image) -> bytes:
    # Baseline-JPEG mit JFIF-Kopf – nur das erkennt LVGLs TJPGD auf dem Gerät
    out = io.BytesIO()
    img.save(out, "JPEG", quality=85)
    return out.getvalue()


def prepare_cover(raw: bytes) -> tuple[bytes, str]:
    """Quadratisch zuschneiden, auf 240 px verkleinern, Durchschnittsfarbe bestimmen."""
    img = square_image(raw, COVER_SIZE)
    r, g, b = img.resize((1, 1), Image.Resampling.BOX).getpixel((0, 0))
    return to_jpeg(img), f"#{r:02x}{g:02x}{b:02x}"


def image_id(url: str, size: int) -> str:
    """Kennung für Bilder aus dem Netz; steht schon in der Liste, bevor das Bild beim Gerät ankommt.
    Die Größe gehört dazu – dasselbe Bild in zwei Größen darf im Gerätespeicher nicht kollidieren."""
    return hashlib.sha1(f"{url}#{size}".encode("utf-8")).hexdigest()[:16]


class Bridge:
    def __init__(self) -> None:
        self.clients: set[web.WebSocketResponse] = set()
        self.devices: set["DeviceClient"] = set()
        self.covers: dict[str, bytes] = {}
        self.images: dict[tuple[str, int], bytes] = {}  # Playlist-/Künstlerbilder fürs Gerät
        self.volume = SpotifyVolume()
        self.mixer = AudioMixer()
        self.spotify = SpotifyAPI()
        self.manager = None
        self.session = None
        self.last_state: dict | None = None
        self._track_key = None
        self._cover_until = 0.0
        self._cover_read_at = 0.0
        self._fast_until = 0.0
        self._poll_wake = asyncio.Event()
        self._cover_url = None
        self._color = "#333333"
        self._modes = None
        # Web-API
        self.api: dict | None = None
        self._liked_uri: str | None = None
        self._liked = False
        self._pending_volume: int | None = None
        self._volume_task: asyncio.Task | None = None
        self._unmute_volume: int | None = None
        self._api_volume_failed = False
        self._api_volume_seen = None
        # Nach einem Songwechsel die API sofort fragen statt bis zu 2 s zu warten (Like-Status sonst spät)
        # Spotify meldet kurz nach dem Verstellen noch die alte Lautstärke; so lange gilt der eigene Wert
        self._volume_set_at = 0.0
        self._volume_set_value: int | None = None
        self._api_wake = asyncio.Event()
        self._api_behind = False
        self._last_context: str | None = None
        self._context_names: dict[str, str | None] = {}

    # ---------- Lesen: Windows ----------

    def _pick_session(self):
        """Nur Spotify, kein anderer Player. Desktop-App meldet sich als "Spotify.exe",
        die Store-Version als "SpotifyAB.SpotifyMusic_…!Spotify"."""
        for s in self.manager.get_sessions():
            if "spotify" in (s.source_app_user_model_id or "").lower():
                return s
        return None

    def _api_fresh(self) -> dict | None:
        if self.spotify.logged_in and self.api and time.monotonic() - self.api["at"] < API_POLL_SECONDS * 3:
            return self.api
        return None

    def _api_volume_ok(self) -> bool:
        api = self._api_fresh()
        return bool(api and api["supports_volume"] and not self._api_volume_failed)

    async def read_state(self) -> dict:
        if self.manager is None:
            self.manager = await SessionManager.request_async()
        session = self.session = self._pick_session()
        spotify_info = {"login": self.spotify.logged_in}
        if session is None:
            # Spotify meldet sich bei Windows erst beim Abspielen; offen, aber still ist etwas anderes als geschlossen
            return {"type": "state", "session": False, "spotify_running": spotify_process_running(), "spotify": spotify_info}

        props = await session.try_get_media_properties_async()
        info = session.get_playback_info()
        timeline = session.get_timeline_properties()
        controls = info.controls
        playing = info.playback_status == PlaybackStatus.PLAYING

        key = (props.title, props.artist, props.album_title)
        if key != self._track_key:
            self._track_key = key
            self._api_wake.set()
            self._cover_until = time.monotonic() + COVER_RETRY_SECONDS
            self._cover_read_at = 0.0
            log.info(
                "Titel: %s – %s | App: %s | kann: seek=%s shuffle=%s repeat=%s | Dauer=%.0fs",
                props.artist, props.title, session.source_app_user_model_id,
                controls.is_playback_position_enabled, controls.is_shuffle_enabled, controls.is_repeat_enabled,
                (timeline.end_time - timeline.start_time).total_seconds(),
            )
        now = time.monotonic()
        if now < self._cover_until and now - self._cover_read_at >= COVER_RETRY_EVERY:
            self._cover_read_at = now
            await self._update_cover(props)

        pos = (timeline.position - timeline.start_time).total_seconds()
        dur = (timeline.end_time - timeline.start_time).total_seconds()
        updated = timeline.last_updated_time
        if playing and updated and updated.year > 2000:
            pos += (datetime.now(timezone.utc) - updated).total_seconds()
        pos = max(0.0, min(pos, dur)) if dur > 0 else max(0.0, pos)

        repeat = info.auto_repeat_mode
        modes = (info.is_shuffle_active, None if repeat is None else int(repeat))
        if modes != self._modes:
            self._modes = modes
            log.info("Spotify meldet: Zufall=%s Wiederholen=%s (0 aus, 1 Titel, 2 Liste)", *modes)

        title, artists = split_featured(props.title, props.artist)
        shuffle = 1 if info.is_shuffle_active else 0
        vol = await self.volume.get()
        volume, muted = (vol[0], vol[1]) if vol else (None, False)
        liked = None
        context = None
        artist_list = None

        api = self._api_fresh()
        # Hängt die API noch beim vorigen Song, schneller nachfragen (siehe api_poll_forever)
        self._api_behind = bool(self.spotify.logged_in and (not api or api["name"].lower() != (props.title or "").lower()))
        if api and api["name"].lower() == (props.title or "").lower():
            # Die API kennt die echte Künstlerliste, Like, Smart Shuffle und woher die Musik kommt
            artists = api["artists"] or artists
            liked = api["liked"]
            context = api["context"]
            artist_list = api["artist_list"]
            self._last_context = context
            if shuffle and api["smart_shuffle"]:
                shuffle = 2
            if self._api_volume_ok() and api["volume"] is not None:
                volume = api["volume"]
                muted = self._unmute_volume is not None and volume == 0

        # Solange die API noch beim vorigen Song hängt, die letzte Herkunft stehen lassen –
        # beim Skippen in derselben Playlist bleibt sie gleich, und die Zeile springt nicht weg und wieder her
        if context is None and self._api_behind:
            context = self._last_context

        return {
            "type": "state",
            "session": True,
            "spotify": spotify_info,
            "app": session.source_app_user_model_id,
            "title": title,
            "artist": artists,
            "album": props.album_title,
            "pos": round(pos, 2),
            "dur": round(dur, 2),
            "playing": playing,
            "shuffle": shuffle,
            "repeat": SMTC_TO_UI_REPEAT.get(int(repeat), 0) if repeat is not None else 0,
            "liked": liked,
            "context": context,
            "artists_list": artist_list,
            "vol": volume,
            "muted": muted,
            "cover": self._cover_url,
            "cover_id": self._cover_url.rsplit("/", 1)[-1].split(".", 1)[0] if self._cover_url else None,
            "color": self._color,
            "can": {
                "seek": controls.is_playback_position_enabled,
                "shuffle": controls.is_shuffle_enabled,
                "repeat": controls.is_repeat_enabled,
                "next": controls.is_next_enabled,
                "prev": controls.is_previous_enabled,
            },
        }

    async def _update_cover(self, props) -> None:
        if not props.thumbnail:
            return
        try:
            jpeg, color = prepare_cover(await read_thumbnail(props.thumbnail))
        except Exception:
            log.exception("Cover konnte nicht gelesen werden")
            return
        digest = hashlib.sha1(jpeg).hexdigest()[:16]
        if digest not in self.covers:
            self.covers[digest] = jpeg
            while len(self.covers) > 30:
                self.covers.pop(next(iter(self.covers)))
        self._cover_url = f"/cover/{digest}.jpg"
        self._color = color

    async def poll_forever(self) -> None:
        while True:
            try:
                state = await self.read_state()
            except Exception:
                log.exception("Lesen fehlgeschlagen")
                self.manager = None
                state = {"type": "state", "session": False, "spotify": {"login": self.spotify.logged_in}}
            self.last_state = state
            await self.broadcast(state)
            fast = time.monotonic() < self._fast_until
            try:
                await asyncio.wait_for(self._poll_wake.wait(), FAST_POLL_SECONDS if fast else POLL_SECONDS)
            except asyncio.TimeoutError:
                pass
            self._poll_wake.clear()

    # ---------- Lesen: Spotify-Web-API ----------

    async def api_poll_forever(self) -> None:
        while True:
            if self.spotify.logged_in:
                try:
                    await self._read_api()
                except SpotifyError as err:
                    log.warning("Spotify-API: %s", err)
                except Exception:
                    log.exception("Spotify-API: Wiedergabestand fehlgeschlagen")
            # regulär alle 2 s; nach Songwechsel sofort, und solange die API hinterherhinkt alle 0,7 s
            try:
                await asyncio.wait_for(self._api_wake.wait(), 0.7 if self._api_behind else API_POLL_SECONDS)
            except asyncio.TimeoutError:
                pass
            self._api_wake.clear()

    async def _read_api(self) -> None:
        player = await self.spotify.player()
        item = (player or {}).get("item")
        if not item:
            self.api = None
            return
        uri = item.get("uri")
        if uri != self._liked_uri:
            self._liked_uri = uri
            self._liked = await self.spotify.is_saved(uri)
        device = player.get("device") or {}
        was_smart = bool(self.api and self.api["smart_shuffle"])
        context = await self._context_name(player.get("context"), item)
        self.api = {
            "at": time.monotonic(),
            "context": context,
            "name": item.get("name") or "",
            "artists": ", ".join(a.get("name", "") for a in item.get("artists") or []),
            "artist_list": [{"id": a.get("id"), "name": a.get("name", "")} for a in item.get("artists") or [] if a.get("id")],
            "uri": uri,
            "liked": self._liked,
            "smart_shuffle": bool(player.get("smart_shuffle")),
            "volume": self._reported_volume(device.get("volume_percent")),
            "supports_volume": device.get("supports_volume", True),
        }
        if self.api["smart_shuffle"] != was_smart:
            log.info("Spotify-API meldet: Smart Shuffle=%s", self.api["smart_shuffle"])
        if self.api["volume"] != self._api_volume_seen:
            self._api_volume_seen = self.api["volume"]
            log.info("Spotify-API meldet: Lautstärke=%s (Gerät: %s, regelbar=%s)",
                     self.api["volume"], device.get("name"), self.api["supports_volume"])

    async def _context_name(self, context: dict | None, item: dict) -> str | None:
        """Woher die Musik kommt, wie Spotify es über dem Titel zeigt. Namen werden gemerkt,
        damit nicht jede Abfrage eine weitere Anfrage kostet."""
        if not context or not context.get("uri"):
            return None
        uri, kind = context["uri"], context.get("type")
        if uri in self._context_names:
            return self._context_names[uri]
        name = None
        try:
            if uri.endswith(":collection") or kind == "collection":
                name = "Lieblingssongs"
            elif kind == "playlist":
                name = await self.spotify.playlist_name(uri.rsplit(":", 1)[-1]) or None
                # Lieblingssongs laufen über eine erzeugte Spotify-Playlist, die in der API immer englisch heißt
                if name == "Liked Songs":
                    name = "Lieblingssongs"
            elif kind == "album":
                name = (item.get("album") or {}).get("name")
            elif kind == "artist":
                name = next((a.get("name") for a in item.get("artists") or [] if a.get("uri") == uri), None)
        except SpotifyError as err:
            log.info("Kontextname für %s nicht lesbar: %s", uri, err)
        self._context_names[uri] = name
        return name

    # ---------- Senden ----------

    async def broadcast(self, message: dict) -> None:
        dead = []
        for ws in self.clients:
            try:
                await ws.send_json(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.clients.discard(ws)
        for device in list(self.devices):
            try:
                await device.push(message, self)
            except Exception:
                self.devices.discard(device)

    async def toast(self, text: str) -> None:
        await self.broadcast({"type": "toast", "text": text})

    # ---------- Steuern ----------

    async def command(self, msg: dict, reply) -> None:
        """reply: async Funktion, die eine Antwort (dict) nur an den anfragenden Client schickt
        (Browser per WebSocket oder Gerät per Rahmen)."""
        cmd, value = msg.get("cmd"), msg.get("value")
        log.info("Befehl: %s %s", cmd, "" if value is None else value)

        if cmd == "volume":
            await self._set_volume(int(value))
        elif cmd == "mute":
            await self._mute(bool(value))
        elif cmd == "like":
            await self._like(bool(value))
        elif cmd == "library":
            await self._send_library(reply)
        elif cmd == "artist":
            await self._send_artist(reply, str(value))
        elif cmd == "keys":
            await reply(device_keys_message())
        elif cmd == "audio_volume":
            await self.mixer.set_volume(str((value or {}).get("id", "")), int((value or {}).get("vol", 0)))
        elif cmd == "audio_mute":
            await self.mixer.set_mute(str((value or {}).get("id", "")), bool((value or {}).get("on")))
        elif cmd == "audio_output":
            await self.mixer.set_output(str(value))
        elif cmd == "key_press":
            await self._press_key(value or {})
        elif cmd == "follow":
            await self._follow(str((value or {}).get("id", "")), bool((value or {}).get("on")))
        elif cmd == "play_context":
            try:
                if value == "liked":
                    await self.spotify.play_liked()
                else:
                    await self.spotify.play_context(str(value))
            except SpotifyError as err:
                log.warning("Playlist starten: %s", err)
                await self.toast(t("Playlist konnte nicht gestartet werden"))
        elif self.session is not None:
            await self._smtc_command(self.session, cmd, value)

    async def _press_key(self, value: dict) -> None:
        try:
            page = load_keys()["pages"][int(value.get("page", 0))]
            key = page["keys"][int(value.get("index", -1))]
        except (IndexError, KeyError, ValueError, TypeError):
            log.warning("Taste unbekannt: %s", value)
            return
        if key is None:
            return  # leeres Feld
        log.info("Taste: %s", key.get("label"))
        try:
            await self.execute_action(key.get("action") or {})
        except Exception as err:
            log.warning("Taste „%s“ fehlgeschlagen: %s", key.get("label"), err)
            await self.toast(t("„{label}“ hat nicht geklappt", label=key.get("label")))

    async def execute_action(self, action: dict) -> None:
        """Gemeinsam für Tastendruck am Gerät und „Testen“ im Editor."""
        if action.get("type") == "media":
            command = action.get("command")
            if command not in MEDIA_COMMANDS:
                raise ValueError(t("unbekannter Medienbefehl"))
            if command.startswith("volume"):
                await asyncio.get_running_loop().run_in_executor(None, send_hotkey, command)
            elif self.session is None:
                raise ValueError(t("Spotify spielt gerade nichts"))
            else:
                await self._smtc_command(self.session, command, None)
            return
        await asyncio.get_running_loop().run_in_executor(None, run_action, action)

    async def push_keys(self) -> None:
        """Neue Belegung sofort an alle Geräte."""
        message = device_keys_message()
        for device in list(self.devices):
            try:
                await device.reply(message)
            except Exception:
                self.devices.discard(device)

    async def _smtc_command(self, s, cmd, value) -> None:
        # Spotify übernimmt den Befehl sofort, meldet den neuen Stand aber verzögert: kurz schnell nachfragen
        self._fast_until = time.monotonic() + FAST_POLL_WINDOW
        self._poll_wake.set()
        if cmd == "play_pause":
            await s.try_toggle_play_pause_async()
        elif cmd == "next":
            await s.try_skip_next_async()
        elif cmd == "prev":
            await s.try_skip_previous_async()
        elif cmd == "seek":
            # Position in 100-ns-Schritten, relativ zum Start der Zeitleiste
            start = s.get_timeline_properties().start_time.total_seconds()
            await s.try_change_playback_position_async(int((start + float(value)) * 10_000_000))
        elif cmd == "shuffle":
            if not await s.try_change_shuffle_active_async(bool(value)):
                await self.toast(t("Spotify erlaubt das über Windows nicht"))
        elif cmd == "repeat":
            mode = UI_TO_SMTC_REPEAT.get(int(value), 0)
            if not await s.try_change_auto_repeat_mode_async(RepeatMode(mode) if RepeatMode else mode):
                await self.toast(t("Spotify erlaubt das über Windows nicht"))

    def _reported_volume(self, spotify_value):
        # Spotify meldet den neuen Wert oft erst Sekunden später; bis dahin sprang der Regler zurück
        target = self._volume_set_value
        if target is None:
            return spotify_value
        confirmed = spotify_value is not None and abs(spotify_value - target) <= 1
        if confirmed or time.monotonic() - self._volume_set_at > VOLUME_HOLD_SECONDS:
            if self._pending_volume is None and (self._volume_task is None or self._volume_task.done()):
                self._volume_set_value = None
            return spotify_value if confirmed or self._volume_set_value is None else target
        return target

    async def _set_volume(self, percent: int) -> None:
        self._unmute_volume = None
        self._volume_set_at = time.monotonic()
        self._volume_set_value = percent
        if not self._api_volume_ok():
            await self.volume.set(percent)
            return
        # Spotify-eigener Regler: Knaufbefehle bündeln, sonst greift das Ratenlimit
        self._pending_volume = percent
        if self.api:
            self.api["volume"] = percent
        if self._volume_task is None or self._volume_task.done():
            self._volume_task = asyncio.create_task(self._flush_volume())

    async def _flush_volume(self) -> None:
        while self._pending_volume is not None:
            await asyncio.sleep(VOLUME_DEBOUNCE)
            percent, self._pending_volume = self._pending_volume, None
            try:
                await self.spotify.set_volume(percent)
                self._volume_set_at = time.monotonic()  # Wartezeit auf die Bestätigung ab dem echten Setzen
                log.info("Spotify-Lautstärke gesetzt: %d %%", percent)
            except SpotifyError as err:
                log.warning("Spotify-Lautstärke: %s, nehme ab jetzt den Windows-Mixer", err)
                self._api_volume_failed = True
                await self.volume.set(percent)

    async def _mute(self, muted: bool) -> None:
        if not self._api_volume_ok():
            await self.volume.mute(muted)
            return
        # Die API kennt kein Stumm; Lautstärke merken und auf 0 setzen
        if muted:
            current = self.api["volume"] if self.api else None
            await self._set_volume(0)
            self._unmute_volume = current if current else 50
        else:
            restore = self._unmute_volume or 50
            await self._set_volume(restore)

    async def _like(self, saved: bool) -> None:
        api = self._api_fresh()
        if not api or not api["uri"]:
            await self.toast(t("Zum Liken erst mit Spotify verbinden"))
            return
        try:
            await self.spotify.set_saved(api["uri"], saved)
        except SpotifyError as err:
            log.warning("Like: %s", err)
            await self.toast(t("Spotify hat das Liken abgelehnt"))
            return
        self._liked = api["liked"] = saved

    async def _send_artist(self, reply, artist_id: str) -> None:
        msg: dict = {"type": "artist", "id": artist_id, "albums": [], "following": None}
        try:
            info = await self.spotify.artist(artist_id)
            images = info.get("images") or []
            msg["name"] = info.get("name", "")
            msg["image"] = images[0]["url"] if images else None
            if self.spotify.has_scope("user-follow-read"):
                msg["following"] = await self.spotify.is_saved(f"spotify:artist:{artist_id}")
            msg["albums"] = await self.spotify.artist_albums(artist_id)
        except SpotifyError as err:
            log.warning("Künstlerseite %s: %s", artist_id, err)
        await reply(msg)

    async def _follow(self, artist_id: str, on: bool) -> None:
        if not artist_id:
            return
        if not self.spotify.has_scope("user-follow-modify"):
            await self.toast(t("Zum Folgen Spotify einmal neu verbinden"))
            return
        try:
            await self.spotify.set_saved(f"spotify:artist:{artist_id}", on)
        except SpotifyError as err:
            log.warning("Folgen: %s", err)
            await self.toast(t("Spotify hat das Folgen abgelehnt"))

    async def fetch_image(self, url: str, size: int) -> bytes | None:
        """Bild aus dem Netz holen und als kleines JPEG fürs Gerät aufbereiten (das Gerät hat kein Internet)."""
        key = (url, size)
        if key in self.images:
            return self.images[key]
        try:
            session = await self.spotify._http()
            async with session.get(url) as resp:
                if resp.status != 200:
                    return None
                raw = await resp.read()
            jpeg = to_jpeg(square_image(raw, size))
        except Exception:
            log.exception("Bild laden fehlgeschlagen: %s", url)
            return None
        self.images[key] = jpeg
        while len(self.images) > 80:
            self.images.pop(next(iter(self.images)))
        return jpeg

    async def _send_library(self, reply) -> None:
        items = []
        try:
            me = await self.spotify.me()
            # Lieblingssongs stehen in Spotify ganz oben, fehlen aber in /me/playlists
            items.append({"name": "Lieblingssongs", "sub": f"Playlist · {me['name']}", "kind": "liked", "image": None, "uri": "liked"})
            items += await self.spotify.playlists()
        except SpotifyError as err:
            log.warning("Bibliothek: %s", err)
        await reply({"type": "library", "items": items, "login": self.spotify.logged_in})


# ---------- Gerät (jetzt der LVGL-Simulator per TCP, später USB/Bluetooth) ----------
# Rahmen: A5 5A | Typ (1 Byte) | Länge (4 Byte, little endian) | Inhalt
FRAME_MAGIC = b"\xA5\x5A"
PC_STATE, PC_COVER, PC_TOAST, PC_LIBRARY, PC_ARTIST, PC_IMAGE, PC_KEYS, PC_ICON = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
PC_AUDIO, PC_PAGES = 0x09, 0x0A
AUDIO_EVERY = 0.1  # Pegel zehnmal pro Sekunde, solange die Audio-Seite offen ist
CARD_IMAGE_SIZE = 264    # Bibliothek
ARTIST_IMAGE_SIZE = 240  # Künstlerfoto (rund, 120 px angezeigt, doppelt für Schärfe beim Zuschnitt)
ALBUM_IMAGE_SIZE = 160   # Alben auf der Künstlerseite
DEV_HELLO, DEV_CMD = 0x10, 0x11
DEVICE_PORT = 8766
MAX_DEVICE_FRAME = 64 * 1024
REPLY_TYPES = {"state": PC_STATE, "toast": PC_TOAST, "library": PC_LIBRARY, "artist": PC_ARTIST, "keys": PC_KEYS}


class DeviceClient:
    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.reader, self.writer = reader, writer
        self.lock = asyncio.Lock()
        self.cover_sent: str | None = None
        self.hello: dict = {}  # was das Gerät beim Verbinden über sich meldet
        self.transport = "Simulator (TCP)"
        self.audio_open = False          # Audio-Seite am Gerät offen: nur dann laufend Pegel schicken
        self.icons_sent: set[str] = set()

    async def send(self, frame_type: int, payload: bytes) -> None:
        async with self.lock:
            self.writer.write(FRAME_MAGIC + bytes([frame_type]) + len(payload).to_bytes(4, "little") + payload)
            await self.writer.drain()

    async def reply(self, message: dict) -> None:
        frame_type = REPLY_TYPES.get(message.get("type"))
        if frame_type is None:
            return
        urls: list[tuple[str, int]] = []
        if message.get("type") == "state":
            # Emojis in Titel, Interpreten und Herkunft kann das Gerät nicht darstellen
            message = {**message, **{k: device_text(message.get(k)) for k in ("title", "artist", "album", "context") if message.get(k)}}
            # Uhrzeit für die Kopfleiste: das Board hat keine eigene Uhr
            message["time"] = int(time.time())
            message["tz"] = int(datetime.now().astimezone().utcoffset().total_seconds())
        if message.get("type") == "library":
            # Das Gerät kann keine Bilder aus dem Netz laden: Kennung in die Liste, Bilder hinterher schicken
            message = {**message, "items": [dict(item) for item in message.get("items", [])]}
            for item in message["items"]:
                item["name"] = device_text(item.get("name"))
                item["sub"] = device_text(item.get("sub"))
                if item.get("image"):
                    item["image_id"] = image_id(item["image"], CARD_IMAGE_SIZE)
                    urls.append((item["image"], CARD_IMAGE_SIZE))
        if message.get("type") == "artist":
            message = {**message, "name": device_text(message.get("name")),
                       "albums": [dict(album) for album in message.get("albums", [])]}
            if message.get("image"):
                message["image_id"] = image_id(message["image"], ARTIST_IMAGE_SIZE)
                urls.append((message["image"], ARTIST_IMAGE_SIZE))
            for album in message["albums"]:
                album["name"] = device_text(album.get("name"))
                if album.get("image"):
                    album["image_id"] = image_id(album["image"], ALBUM_IMAGE_SIZE)
                    urls.append((album["image"], ALBUM_IMAGE_SIZE))
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        await self.send(frame_type, body)
        for url, size in urls:
            jpeg = await bridge.fetch_image(url, size)
            if jpeg:
                await self.send(PC_IMAGE, image_id(url, size).encode("ascii") + jpeg)
        if message.get("type") == "keys":
            # Emoji- und Bildsymbole direkt hinter der Belegung: 16 Zeichen Kennung | Breite | Höhe | RGB565A8
            icon_ids = {k["icon_id"] for page in message["pages"] for k in page["keys"] if k.get("icon_id")}
            for icon_id in icon_ids:
                await self.send(PC_ICON, icon_id.encode("ascii") + key_icons[icon_id])

    async def send_pages(self) -> None:
        body = json.dumps(paths.device_pages(), separators=(",", ":")).encode("utf-8")
        await self.send(PC_PAGES, body)

    async def send_audio(self, snapshot: dict, mixer: AudioMixer, layout: str) -> None:
        # Programmsymbole einmal pro Verbindung, vor der Liste, die sie benutzt
        for app in snapshot["apps"]:
            icon = app.get("icon_id")
            if icon and icon not in self.icons_sent and icon in mixer.icons:
                await self.send(PC_ICON, icon.encode("ascii") + mixer.icons[icon])
                self.icons_sent.add(icon)
        message = {**snapshot, "layout": layout,
                   "apps": [{**app, "name": device_text(app["name"])} for app in snapshot["apps"]]}
        await self.send(PC_AUDIO, json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))

    async def push(self, message: dict, source: "Bridge") -> None:
        await self.reply(message)
        # Cover nur schicken, wenn es für dieses Gerät neu ist: 16 Zeichen Kennung + JPEG
        cover_id = message.get("cover_id") if message.get("type") == "state" else None
        if cover_id and cover_id != self.cover_sent and cover_id in source.covers:
            await self.send(PC_COVER, cover_id.encode("ascii") + source.covers[cover_id])
            self.cover_sent = cover_id


bridge = Bridge()


async def read_frame_header(reader: asyncio.StreamReader) -> bytes:
    """Liest bis zum nächsten Rahmenanfang A5 5A. Fremde Bytes davor – etwa Startmeldungen des Chips über
    USB – werden übersprungen statt die Verbindung zu trennen (sonst verbindet sich USB im Sekundentakt neu)."""
    skipped = bytearray()
    previous = await reader.readexactly(1)
    while True:
        current = await reader.readexactly(1)
        if previous == b"\xA5" and current == b"\x5A":
            break
        if len(skipped) < 300:
            skipped += previous
        previous = current
    if skipped:
        log.info("Gerät: %d fremde Bytes übersprungen: %r", len(skipped), bytes(skipped[:120]))
    raw_log = os.environ.get("DECK_THING_RAW_LOG")  # Fehlersuche: Fremdbytes (z. B. Absturzmeldungen) vollständig sichern
    if raw_log and skipped:
        with open(raw_log, "ab") as out:
            out.write(bytes(skipped) + b"\n----\n")
    return FRAME_MAGIC + await reader.readexactly(5)


async def device_connection(reader: asyncio.StreamReader, writer, transport: str = "Simulator (TCP)") -> None:
    device = DeviceClient(reader, writer)
    device.transport = transport
    bridge.devices.add(device)
    log.info("Gerät verbunden: %s (%s)", transport, writer.get_extra_info("peername"))
    try:
        await device.send_pages()
        if bridge.last_state:
            await device.push(bridge.last_state, bridge)
        while True:
            header = await read_frame_header(reader)
            length = int.from_bytes(header[3:7], "little")
            if length > MAX_DEVICE_FRAME:
                log.warning("Gerät: Rahmen zu groß (%d Bytes), trenne", length)
                break
            payload = await reader.readexactly(length)
            if header[2] == DEV_HELLO:
                log.info("Gerät meldet sich: %s", payload.decode("utf-8", "replace"))
                try:
                    device.hello = json.loads(payload)
                except ValueError:
                    device.hello = {}
            elif header[2] == DEV_CMD:
                try:
                    msg = json.loads(payload)
                    if msg.get("cmd") == "audio":  # Audio-Seite auf/zu: gilt nur für dieses Gerät
                        device.audio_open = bool(msg.get("value"))
                        log.info("Audio-Seite %s", "offen" if device.audio_open else "zu")
                        continue
                    await bridge.command(msg, device.reply)
                except Exception:
                    log.exception("Gerätebefehl fehlgeschlagen: %r", payload[:200])
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        bridge.devices.discard(device)
        writer.close()
        log.info("Gerät getrennt: %s", transport)


# ---------- Gerät per USB ----------
# Das Board meldet sich über den eingebauten USB-Serial-Anschluss des ESP32-S3 (Buchse „USB“)
USB_IDS = {(0x303A, 0x1001)}
USB_SCAN_SECONDS = 2.0


class SerialWriter:
    """Stellt einen seriellen Port wie einen asyncio-StreamWriter dar, damit device_connection() gleich bleibt."""

    def __init__(self, port, loop: asyncio.AbstractEventLoop) -> None:
        self._port = port
        self._loop = loop
        self._pending = bytearray()
        self._closed = False

    def write(self, data: bytes) -> None:
        self._pending += data

    async def drain(self) -> None:
        if not self._pending:
            return
        data, self._pending = bytes(self._pending), bytearray()
        await self._loop.run_in_executor(None, self._port.write, data)

    def get_extra_info(self, name: str, default=None):
        return self._port.port if name == "peername" else default

    def close(self) -> None:
        if not self._closed:
            self._closed = True
            try:
                self._port.close()
            except Exception:
                pass


def open_usb_port(device: str):
    import serial

    port = serial.Serial()
    port.port = device
    port.baudrate = 115200  # beim USB-Serial des ESP32-S3 ohne Bedeutung
    port.timeout = 0.1
    port.write_timeout = 3
    # DTR/RTS nicht setzen: über diese Leitungen würde der ESP32-S3 neu starten oder in den Flash-Modus gehen
    port.dtr = False
    port.rts = False
    port.open()
    return port


async def run_usb_device(port, open_ports: set[str]) -> None:
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader()
    stop = threading.Event()

    def read_loop() -> None:
        try:
            while not stop.is_set():
                data = port.read(4096)
                if data:
                    loop.call_soon_threadsafe(reader.feed_data, data)
        except Exception:
            pass  # Kabel gezogen oder Port geschlossen
        loop.call_soon_threadsafe(reader.feed_eof)

    threading.Thread(target=read_loop, name=f"USB {port.port}", daemon=True).start()
    try:
        await device_connection(reader, SerialWriter(port, loop), transport="USB")
    finally:
        stop.set()
        open_ports.discard(port.port)


async def usb_devices_forever() -> None:
    """Sucht alle zwei Sekunden nach angesteckten Boards und verbindet sich mit neuen."""
    from serial.tools import list_ports

    loop = asyncio.get_running_loop()
    open_ports: set[str] = set()
    while True:
        try:
            for info in list_ports.comports():
                if (info.vid, info.pid) not in USB_IDS or info.device in open_ports:
                    continue
                try:
                    port = await loop.run_in_executor(None, open_usb_port, info.device)
                except Exception as err:
                    log.info("USB-Gerät %s noch nicht bereit: %s", info.device, err)
                    continue
                open_ports.add(info.device)
                asyncio.create_task(run_usb_device(port, open_ports))
        except Exception:
            log.exception("USB-Suche fehlgeschlagen")
        await asyncio.sleep(USB_SCAN_SECONDS)

# ---------- Webseiten ----------

PAGE = """<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Spotify verbinden</title>
<style>
body{{margin:0;background:#121212;color:#fff;font:16px/1.5 "Segoe UI",system-ui,sans-serif}}
main{{max-width:620px;margin:48px auto;padding:0 24px}}
h1{{font-size:28px;margin:0 0 8px}} p,li{{color:#b3b3b3}} b,code{{color:#fff}}
ol{{padding-left:20px}} li{{margin:10px 0}}
code{{background:#282828;padding:2px 6px;border-radius:4px;user-select:all}}
input{{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;border:1px solid #535353;background:#181818;color:#fff;font:inherit}}
button,.btn{{display:inline-block;margin-top:14px;padding:12px 26px;border:0;border-radius:999px;background:#1ed760;color:#000;font:600 16px "Segoe UI",sans-serif;cursor:pointer;text-decoration:none}}
.ghost{{background:transparent;color:#b3b3b3;border:1px solid #535353}}
.ok{{color:#1ed760}} .err{{color:#f3727f}}
</style></head><body><main>{body}</main></body></html>"""


def page(body: str) -> web.Response:
    return web.Response(text=PAGE.format(body=body), content_type="text/html")


async def index(_request: web.Request) -> web.StreamResponse:
    raise web.HTTPFound("/app")


async def cover(request: web.Request) -> web.Response:
    data = bridge.covers.get(request.match_info["digest"])
    if data is None:
        raise web.HTTPNotFound()
    return web.Response(body=data, content_type="image/jpeg", headers={"Cache-Control": "max-age=86400"})


FONT_DIR = RES / "web" / "fonts"


async def keys_page(_request: web.Request) -> web.StreamResponse:
    return web.FileResponse(RES / "web" / "keys.html", headers={"Cache-Control": "no-store"})


async def i18n_script(_request: web.Request) -> web.StreamResponse:
    return web.FileResponse(RES / "web" / "i18n.js", headers={"Cache-Control": "no-store"})


async def api_lang(_request: web.Request) -> web.Response:
    """Sprache für die Seiten, bevor sie etwas zeigen (die Einstellung speichert das App-Fenster)."""
    return web.json_response({"lang": i18n.current(), "setting": i18n.setting()}, headers={"Cache-Control": "no-store"})


async def lucide_file(request: web.Request) -> web.StreamResponse:
    name = request.match_info["name"]
    if name not in ("lucide.css", "lucide.ttf", "Figtree-Medium.ttf", "Figtree-SemiBold.ttf", "Figtree-Bold.ttf",
                    "Figtree-ExtraBold.ttf"):
        raise web.HTTPNotFound()
    path = FONT_DIR / name
    if not path.exists():
        raise web.HTTPNotFound()
    return web.FileResponse(path)


async def api_keys_get(_request: web.Request) -> web.Response:
    return web.json_response({"config": load_keys(), "icons": KEY_ICONS})


async def api_keys_put(request: web.Request) -> web.Response:
    try:
        data = await request.json()
    except ValueError:
        return web.json_response({"error": t("Ungültige Daten")}, status=400)
    error = validate_keys(data)
    if error:
        return web.json_response({"error": error}, status=400)
    save_keys(data)
    log.info("Tastenbelegung gespeichert")
    await bridge.push_keys()
    return web.json_response({"ok": True})


async def api_keys_test(request: web.Request) -> web.Response:
    try:
        action = await request.json()
        await bridge.execute_action(action)
    except Exception as err:
        return web.json_response({"error": t("Hat nicht geklappt: {err}", err=err)}, status=400)
    return web.json_response({"ok": True})


async def api_key_image_upload(request: web.Request) -> web.Response:
    """Eigenes Bild für eine Taste: verkleinert als PNG ablegen, der Name ist die Prüfsumme."""
    raw = await request.read()
    try:
        img = Image.open(io.BytesIO(raw))
        img.load()
        img = img.convert("RGBA")
    except Exception:
        return web.json_response({"error": t("Das Bild lässt sich nicht lesen (PNG, JPG, WebP, GIF oder ICO)")}, status=400)
    img.thumbnail((256, 256), Image.Resampling.LANCZOS)
    out = io.BytesIO()
    img.save(out, "PNG")
    data = out.getvalue()
    name = hashlib.sha1(data).hexdigest()[:16] + ".png"
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    (ICON_DIR / name).write_bytes(data)
    return web.json_response({"image": name})


async def api_key_image(request: web.Request) -> web.StreamResponse:
    name = request.match_info["name"]
    if not IMAGE_NAME_RE.fullmatch(name) or not (ICON_DIR / name).exists():
        raise web.HTTPNotFound()
    return web.FileResponse(ICON_DIR / name)


async def app_page(_request: web.Request) -> web.StreamResponse:
    return web.FileResponse(RES / "web" / "app.html", headers={"Cache-Control": "no-store"})


async def app_asset(request: web.Request) -> web.StreamResponse:
    name = request.match_info["name"]
    if name not in ("deck_thing.png",):
        raise web.HTTPNotFound()
    return web.FileResponse(RES / "assets" / name)


async def api_status(_request: web.Request) -> web.Response:
    """Überblick für die App: Gerät, Spotify, was läuft, Tasten."""
    state = bridge.last_state or {}
    name = None
    if bridge.spotify.logged_in:
        try:
            name = (await bridge.spotify.me()).get("name")
        except (SpotifyError, aiohttp.ClientError, asyncio.TimeoutError):
            pass
    keys = load_keys()["pages"][0]["keys"]
    return web.json_response({
        "devices": [{"transport": d.transport, "firmware": d.hello.get("fw"), "knob": d.hello.get("has_knob"),
                     "width": d.hello.get("w"), "height": d.hello.get("h")} for d in bridge.devices],
        "spotify": {"login": bridge.spotify.logged_in, "client_id": bridge.spotify.client_id, "name": name,
                    "running": bool(state.get("session") or state.get("spotify_running"))},
        "playing": {k: state.get(k) for k in ("title", "artist", "context", "cover", "playing")} if state.get("session") else None,
        "keys": {"used": sum(1 for k in keys if k), "slots": len(keys)},
        "redirect_uri": REDIRECT_URI,
        "lang": i18n.current(),
    })


async def api_spotify_login(request: web.Request) -> web.Response:
    """Anmelde-Adresse für den Browser; die App öffnet sie im Standardbrowser, wo man bei Spotify schon angemeldet ist."""
    try:
        client_id = str((await request.json()).get("client_id", "")).strip()
    except ValueError:
        client_id = ""
    if not re.fullmatch(r"[0-9a-fA-F]{32}", client_id):
        return web.json_response({"error": t("Die Client-ID besteht aus 32 Zeichen (0–9, a–f).")}, status=400)
    return web.json_response({"url": bridge.spotify.login_url(client_id.lower())})


async def api_spotify_logout(_request: web.Request) -> web.Response:
    bridge.spotify.logout()
    bridge.api = None
    log.info("Spotify getrennt")
    return web.json_response({"ok": True})


async def spotify_setup(request: web.Request) -> web.Response:
    if request.method == "POST":
        form = await request.post()
        client_id = str(form.get("client_id", "")).strip()
        if not re.fullmatch(r"[0-9a-f]{32}", client_id):
            return page('<h1>Spotify verbinden</h1><p class="err">Die Client-ID besteht aus 32 Zeichen (0–9, a–f). '
                        'Bitte noch einmal kopieren.</p><a class="btn ghost" href="/spotify/setup">Zurück</a>')
        raise web.HTTPFound(bridge.spotify.login_url(client_id))

    if bridge.spotify.logged_in:
        return page('<h1>Spotify ist verbunden</h1><p class="ok">Like, Bibliothek, Smart Shuffle und die '
                    'Spotify-Lautstärke laufen jetzt über dein Konto.</p>'
                    '<a class="btn" href="/">Zum Gerät</a> <a class="btn ghost" href="/spotify/logout">Trennen</a>')

    client_id = html.escape(bridge.spotify.client_id)
    return page(f"""
<h1>Spotify verbinden</h1>
<p>Für Like, deine Playlists, Smart Shuffle und den Lautstärkeregler in Spotify braucht die App einmal Zugriff
auf dein Konto. Dafür legst du eine eigene, kostenlose Entwickler-App an. <b>Voraussetzung ist Spotify Premium.</b></p>
<ol>
  <li>Öffne <a class="ok" href="https://developer.spotify.com/dashboard" target="_blank" rel="noopener">developer.spotify.com/dashboard</a>
      und melde dich mit deinem Spotify-Konto an.</li>
  <li>Klicke auf <b>Create app</b>. Name und Beschreibung sind egal, z. B. „Deck Thing“.</li>
  <li>Trage als <b>Redirect URI</b> genau das ein und klicke auf <b>Add</b>:<br><code>{REDIRECT_URI}</code></li>
  <li>Setze bei <b>Which API/SDKs are you planning to use?</b> den Haken bei <b>Web API</b>, stimme den Bedingungen zu und speichere.</li>
  <li>Kopiere auf der Seite der App die <b>Client ID</b> und füge sie hier ein:</li>
</ol>
<form method="post">
  <input name="client_id" placeholder="Client ID (32 Zeichen)" value="{client_id}" autocomplete="off" spellcheck="false">
  <button type="submit">Mit Spotify verbinden</button>
</form>
<p>Ein Client-Secret wird nicht gebraucht. Die Zugangsdaten bleiben auf diesem PC.</p>""")


async def spotify_callback(request: web.Request) -> web.Response:
    if "error" in request.query:
        return page(f'<h1>Nicht verbunden</h1><p class="err">Spotify meldet: {html.escape(request.query["error"])}</p>'
                    '<a class="btn ghost" href="/spotify/setup">Noch einmal</a>')
    try:
        await bridge.spotify.exchange_code(request.query.get("code", ""), request.query.get("state", ""))
    except SpotifyError as err:
        log.warning("Anmeldung: %s", err)
        return page(f'<h1>Nicht verbunden</h1><p class="err">{html.escape(str(err))}</p>'
                    '<a class="btn ghost" href="/spotify/setup">Noch einmal</a>')
    log.info("Spotify verbunden")
    bridge._api_volume_failed = False
    return page('<h1>Spotify ist verbunden</h1><p class="ok">Du kannst dieses Browserfenster schließen '
                'und zur App zurückkehren.</p>')


async def spotify_logout(_request: web.Request) -> web.Response:
    bridge.spotify.logout()
    bridge.api = None
    log.info("Spotify getrennt")
    raise web.HTTPFound("/spotify/setup")


async def websocket(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(heartbeat=20)
    await ws.prepare(request)
    bridge.clients.add(ws)
    log.info("Browser verbunden (%d)", len(bridge.clients))
    try:
        if bridge.last_state:
            await ws.send_json(bridge.last_state)
        async for msg in ws:
            if msg.type == WSMsgType.TEXT:
                try:
                    await bridge.command(json.loads(msg.data), ws.send_json)
                except Exception:
                    log.exception("Befehl fehlgeschlagen: %s", msg.data)
    finally:
        bridge.clients.discard(ws)
        log.info("Browser getrennt (%d)", len(bridge.clients))
    return ws


async def audio_forever() -> None:
    """Mixer ans Gerät, solange dort die Audio-Seite offen ist."""
    while True:
        viewers = [d for d in bridge.devices if d.audio_open]
        if not viewers:
            await asyncio.sleep(0.2)
            continue
        started = time.monotonic()
        try:
            snapshot = await bridge.mixer.snapshot()
        except Exception:
            log.exception("Audio lesen fehlgeschlagen")
            await asyncio.sleep(1.0)
            continue
        layout = paths.audio_layout()
        for device in viewers:
            try:
                await device.send_audio(snapshot, bridge.mixer, layout)
            except Exception:
                device.audio_open = False  # Gerät weg; beim Wiederverbinden meldet es die Seite neu
        await asyncio.sleep(max(0.02, AUDIO_EVERY - (time.monotonic() - started)))


async def settings_forever() -> None:
    """Seitenauswahl aus den Einstellungen sofort ans Gerät, wenn sie sich im App-Fenster ändert."""
    last = paths.device_pages()
    while True:
        await asyncio.sleep(1.0)
        pages = paths.device_pages()
        if pages != last:
            last = pages
            for device in list(bridge.devices):
                try:
                    await device.send_pages()
                except Exception:
                    pass


async def on_startup(app: web.Application) -> None:
    def quiet_resets(loop, context):
        # Neu laden im Browser kappt die Verbindung hart; unter Windows meldet asyncio das als Fehler
        if isinstance(context.get("exception"), ConnectionResetError):
            return
        loop.default_exception_handler(context)

    asyncio.get_running_loop().set_exception_handler(quiet_resets)
    app["tasks"] = [asyncio.create_task(bridge.poll_forever()), asyncio.create_task(bridge.api_poll_forever()),
                    asyncio.create_task(usb_devices_forever()), asyncio.create_task(audio_forever()),
                    asyncio.create_task(settings_forever())]
    app["device_server"] = await asyncio.start_server(device_connection, HOST, DEVICE_PORT)
    log.info("Geräte-Eingang auf %s:%d (Simulator, später USB/Bluetooth)", HOST, DEVICE_PORT)
    log.info("Spotify-Anmeldung: %s", "verbunden" if bridge.spotify.logged_in else "nicht verbunden (http://127.0.0.1:8765/spotify/setup)")


async def on_cleanup(app: web.Application) -> None:
    for task in app["tasks"]:
        task.cancel()
    app["device_server"].close()
    await bridge.spotify.close()


def create_app() -> web.Application:
    """Webserver der Brücke; läuft allein (python bridge.py) oder eingebettet in der PC-App."""
    app = web.Application(client_max_size=MAX_ICON_UPLOAD)
    app.router.add_get("/", index)
    app.router.add_get("/app", app_page)
    app.router.add_get("/assets/{name}", app_asset)
    app.router.add_get("/api/status", api_status)
    app.router.add_post("/api/spotify/login", api_spotify_login)
    app.router.add_post("/api/spotify/logout", api_spotify_logout)
    app.router.add_get("/cover/{digest}.jpg", cover)
    app.router.add_get("/keys", keys_page)
    app.router.add_get("/i18n.js", i18n_script)
    app.router.add_get("/api/lang", api_lang)
    app.router.add_get("/fonts/{name}", lucide_file)
    app.router.add_get("/api/keys", api_keys_get)
    app.router.add_put("/api/keys", api_keys_put)
    app.router.add_post("/api/keys/test", api_keys_test)
    app.router.add_post("/api/keys/image", api_key_image_upload)
    app.router.add_get("/api/keys/image/{name}", api_key_image)
    app.router.add_route("*", "/spotify/setup", spotify_setup)
    app.router.add_get("/spotify/logout", spotify_logout)
    app.router.add_get("/callback", spotify_callback)
    app.router.add_get("/ws", websocket)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s  %(message)s", datefmt="%H:%M:%S")
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)
    app = create_app()
    url = f"http://{HOST}:{PORT}"
    log.info("Brücke läuft unter %s  (Beenden mit Strg+C)", url)
    if "--no-browser" not in sys.argv:  # offene Tabs verbinden sich von selbst neu
        webbrowser.open(url)
    web.run_app(app, host=HOST, port=PORT, print=None)


if __name__ == "__main__":
    main()
