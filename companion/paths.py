"""Where the app keeps its data: ~/.deck-thing (key layout, pictures, Spotify login, settings, log).

Never in the program or project folder – so everything survives updates and never lands in the repo by accident.
"""

from __future__ import annotations

import json
import shutil
from pathlib import Path

DATA_DIR = Path.home() / ".deck-thing"
SETTINGS_FILE = DATA_DIR / "settings.json"  # written by the app window, the bridge only reads it
_LEGACY_DIR = Path.home() / ".carthing-pc"  # folder name from the draft phase


def _migrate() -> None:
    """Take over once what the draft already created (copy; the original stays where it is)."""
    if DATA_DIR.exists() or not _LEGACY_DIR.is_dir():
        return
    try:
        shutil.copytree(_LEGACY_DIR, DATA_DIR, ignore=shutil.ignore_patterns("webview", "*.log", "*.tmp"))
    except OSError:
        pass


_migrate()


# Which pages the device shows as tabs at the top; at least one stays on
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
