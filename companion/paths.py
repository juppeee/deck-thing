"""Wo die App ihre Daten ablegt: ~/.deck-thing (Tastenbelegung, Bilder, Spotify-Anmeldung, Einstellungen, Protokoll).

Nie im Programm- oder Projektordner – so überlebt alles Updates und landet nicht versehentlich im Repo.
"""

from __future__ import annotations

import json
import shutil
from pathlib import Path

DATA_DIR = Path.home() / ".deck-thing"
SETTINGS_FILE = DATA_DIR / "settings.json"  # schreibt das App-Fenster, die Brücke liest nur
_LEGACY_DIR = Path.home() / ".carthing-pc"  # Ordnername aus der Entwurfsphase


def _migrate() -> None:
    """Einmalig übernehmen, was der Entwurf schon angelegt hat (kopieren, das Original bleibt liegen)."""
    if DATA_DIR.exists() or not _LEGACY_DIR.is_dir():
        return
    try:
        shutil.copytree(_LEGACY_DIR, DATA_DIR, ignore=shutil.ignore_patterns("webview", "*.log", "*.tmp"))
    except OSError:
        pass


_migrate()


# Welche Seiten das Gerät oben als Reiter zeigt; mindestens eine bleibt an
DEFAULT_PAGES = {"music": True, "playlists": True, "keys": True, "audio": True}
AUDIO_LAYOUTS = ("mixer", "list")


def read_settings() -> dict:
    try:
        data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def device_pages(settings: dict | None = None) -> dict:
    stored = (settings if settings is not None else read_settings()).get("pages")
    pages = {k: bool(stored.get(k, v)) if isinstance(stored, dict) else v for k, v in DEFAULT_PAGES.items()}
    return pages if any(pages.values()) else dict(DEFAULT_PAGES)


def audio_layout(settings: dict | None = None) -> str:
    value = (settings if settings is not None else read_settings()).get("audio_layout")
    return value if value in AUDIO_LAYOUTS else "mixer"
