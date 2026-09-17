"""App language: German or English, following the Windows display language by default.

Like the Claude Session Browser, the German sentence is the key: the code says t("Speichern"),
and a missing translation shows German. The interface (web/i18n.js) has its own table;
this one only holds text that Python produces (errors, hints on the device, tray menu).
"""

from __future__ import annotations

import ctypes
import json

import paths

SETTINGS_FILE = paths.DATA_DIR / "settings.json"
CHOICES = ("auto", "de", "en")

EN = {
    # Tray menu
    "Öffnen": "Open",
    "Beenden": "Quit",
    # Key layout
    "Emoji fehlt": "Emoji missing",
    "bitte genau ein Emoji, keine Buchstaben": "please use exactly one emoji, no letters",
    "dieses Zeichen kann Windows nicht darstellen": "Windows can't draw this character",
    "unbekannte Taste „{part}“": "unknown key “{part}”",
    "keine Tastenkombination eingetragen": "no key combination entered",
    "kein Programm eingetragen": "no program entered",
    "Skript nicht gefunden: {path}": "script not found: {path}",
    "Skripte: .ps1, .bat, .cmd oder .py": "scripts: .ps1, .bat, .cmd or .py",
    "Link muss mit https:// o. Ä. beginnen": "link must start with https:// or similar",
    "unbekannte Aktion „{kind}“": "unknown action “{kind}”",
    "unbekannter Medienbefehl": "unknown media command",
    "Spotify spielt gerade nichts": "Spotify isn't playing anything",
    "Ungültige Daten": "Invalid data",
    "Hat nicht geklappt: {err}": "Didn't work: {err}",
    "Das Bild lässt sich nicht lesen (PNG, JPG, WebP, GIF oder ICO)": "Can't read this image (PNG, JPG, WebP, GIF or ICO)",
    "Die Client-ID besteht aus 32 Zeichen (0–9, a–f).": "The client ID has 32 characters (0–9, a–f).",
    # Hints on the device
    "Playlist konnte nicht gestartet werden": "Couldn't start the playlist",
    "„{label}“ hat nicht geklappt": "“{label}” didn't work",
    "Spotify erlaubt das über Windows nicht": "Spotify doesn't allow this through Windows",
    "Zum Liken erst mit Spotify verbinden": "Connect Spotify first to like songs",
    "Spotify hat das Liken abgelehnt": "Spotify refused the like",
    "Zum Folgen Spotify einmal neu verbinden": "Reconnect Spotify once to follow artists",
    "Spotify hat das Folgen abgelehnt": "Spotify refused the follow",
    # Updates
    "Keine Update-Information – bitte erst nach Updates suchen.": "No update information – please check for updates first.",
    "Im Entwicklungsmodus wird nicht aktualisiert.": "Development mode doesn't update.",
    "Update ohne gültige Prüfsumme – abgebrochen.": "Update has no valid checksum – cancelled.",
    "deck_updater.exe fehlt – bitte die App neu installieren.": "deck_updater.exe is missing – please reinstall the app.",
    "Update läuft bereits.": "Update is already running.",
    "Download unvollständig": "Download incomplete",
    "Prüfsumme stimmt nicht – Update abgebrochen": "Checksum doesn't match – update cancelled",
    # Audio page
    "Gesamt": "Master",
    "Mikrofon": "Microphone",
    "Systemklänge": "System sounds",
    # File picker
    "Programme (*.exe;*.lnk;*.bat;*.cmd)": "Programs (*.exe;*.lnk;*.bat;*.cmd)",
    "Skripte (*.ps1;*.bat;*.cmd;*.py)": "Scripts (*.ps1;*.bat;*.cmd;*.py)",
    "Alle Dateien (*.*)": "All files (*.*)",
}


def windows_language() -> str:
    """"de" on German Windows, "en" everywhere else (primary language id 0x07 = German)."""
    try:
        return "de" if ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF == 0x07 else "en"
    except (AttributeError, OSError):
        return "en"


def setting() -> str:
    try:
        value = json.loads(SETTINGS_FILE.read_text(encoding="utf-8")).get("language", "auto")
    except (OSError, ValueError):
        value = "auto"
    return value if value in CHOICES else "auto"


def current() -> str:
    value = setting()
    return windows_language() if value == "auto" else value


def t(text: str, **values) -> str:
    out = EN.get(text, text) if current() == "en" else text
    return out.format(**values) if values else out
