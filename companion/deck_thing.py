"""Deck Thing – PC-App.

Eigenes Fenster (pywebview/WebView2) statt Browser, Symbol im Infobereich, nur eine Instanz,
Autostart und Auto-Update. Die Brücke (bridge.py) läuft unsichtbar im selben Prozess:
sie liest Spotify, spricht mit dem Gerät und liefert die Seiten des Fensters aus.

Aufbau wie beim Claude Session Browser; dessen Lehren sind übernommen
(Updater als eigener Prozess, Mutex + Sperrdatei, Fenster über das Handle zurückholen).

Start:  python deck_thing.py          (Fenster)
        python deck_thing.py --tray   (nur Infobereich, wie beim Autostart)
"""

from __future__ import annotations

import asyncio
import ctypes
import hashlib
import json
import logging
import os
import re
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import webbrowser
from pathlib import Path

import i18n
import paths
from i18n import t

VERSION = "0.1.0"
APP_TITLE = "Deck Thing"
APP_ID = "DeckThing"
EXE_NAME = "DeckThing.exe"
# version.json auf main; solange das Repo privat ist, schlägt die Abfrage fehl (die App meldet das nur)
UPDATE_URL = "https://raw.githubusercontent.com/juppeee/deck-thing/main/version.json"
RELEASES_URL = "https://github.com/juppeee/deck-thing/releases/latest"

FROZEN = bool(getattr(sys, "frozen", False))
INSTALL_DIR = Path(os.environ.get("LOCALAPPDATA") or Path.home()) / "Programs" / APP_ID
# Autostart und Updates nur für die installierte App – ein Test-Build aus dist\ soll sich nicht eintragen
INSTALLED = FROZEN and Path(sys.executable).resolve().parent == INSTALL_DIR.resolve()
RES = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
DATA_DIR = paths.DATA_DIR
SETTINGS_FILE = DATA_DIR / "settings.json"
LOG_FILE = DATA_DIR / "deck_thing.log"
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
MUTEX_NAME = "Local\\DeckThing_SingleInstance_juppeee"
LOCK_FILE = Path(os.environ.get("LOCALAPPDATA") or Path.home()) / f"{APP_ID}.instance.lock"
DEFAULT_SETTINGS = {"close_to_tray": True, "autostart": True, "language": "auto", "pages": dict(paths.DEFAULT_PAGES),
                    "audio_layout": "mixer", "win_w": 1180, "win_h": 800}
DETACHED = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP

log = logging.getLogger("app")


# ---------- Grundlagen ----------

def setup_logging() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    try:
        if LOG_FILE.stat().st_size > 2 * 1024 * 1024:
            LOG_FILE.unlink()
    except OSError:
        pass
    handlers: list[logging.Handler] = [logging.FileHandler(LOG_FILE, encoding="utf-8")]
    if not FROZEN:
        handlers.append(logging.StreamHandler())
    logging.basicConfig(level=logging.INFO, format="%(asctime)s  %(name)s  %(message)s",
                        datefmt="%H:%M:%S", handlers=handlers, force=True)
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)


def load_settings() -> dict:
    try:
        stored = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        stored = {}
    return {**DEFAULT_SETTINGS, **{k: v for k, v in stored.items() if k in DEFAULT_SETTINGS}}


def save_settings(settings: dict) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    tmp = SETTINGS_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    tmp.replace(SETTINGS_FILE)


def autostart_command() -> str:
    return f'"{sys.executable}" --tray'


def is_autostart() -> bool:
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            return winreg.QueryValueEx(key, APP_ID)[0] == autostart_command()
    except OSError:
        return False


def set_autostart(enabled: bool) -> bool:
    """Nur in der installierten App – sonst stünde beim Anmelden python.exe oder ein Test-Build im Autostart."""
    if not INSTALLED:
        return False
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
            if enabled:
                winreg.SetValueEx(key, APP_ID, 0, winreg.REG_SZ, autostart_command())
            else:
                try:
                    winreg.DeleteValue(key, APP_ID)
                except FileNotFoundError:
                    pass
        return True
    except OSError as err:
        log.warning("Autostart ließ sich nicht setzen: %s", err)
        return False


# ---------- Nur eine Instanz ----------

_instance_handles: list = []  # Mutex und Sperrdatei bis zum Prozessende festhalten


def acquire_single_instance() -> bool:
    """Mutex und zusätzlich eine gesperrte Datei: nur wenn beide frei sind, sind wir die erste Instanz."""
    first = True
    try:
        k32 = ctypes.windll.kernel32
        k32.CreateMutexW.restype = ctypes.c_void_p
        k32.SetLastError(0)
        handle = k32.CreateMutexW(None, False, MUTEX_NAME)
        if handle and k32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
            first = False
        _instance_handles.append(handle)
    except Exception:
        pass
    try:
        import msvcrt

        lock = open(LOCK_FILE, "wb")
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            _instance_handles.append(lock)
        except OSError:
            first = False
            lock.close()
    except OSError:
        pass
    return first


def restore_existing_window() -> bool:
    """Fenster der laufenden Instanz nach vorn holen – auch aus dem Infobereich."""
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    found = {"hwnd": 0}
    proc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)

    def enum(hwnd, _lparam):
        length = user32.GetWindowTextLengthW(hwnd)
        if length > 0:
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            if buf.value == APP_TITLE:
                found["hwnd"] = hwnd
                return False
        return True

    user32.EnumWindows(proc(enum), 0)
    hwnd = found["hwnd"]
    if not hwnd:
        return False
    user32.ShowWindow(hwnd, 9 if user32.IsIconic(hwnd) else 5)  # SW_RESTORE / SW_SHOW
    # SetForegroundWindow allein wird von Windows oft verweigert; kurz ganz nach oben hilft
    flags = 0x0002 | 0x0001 | 0x0040  # NOMOVE | NOSIZE | SHOWWINDOW
    user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, flags)
    user32.SetWindowPos(hwnd, -2, 0, 0, 0, 0, flags)
    user32.SetForegroundWindow(hwnd)
    return True


# ---------- Brücke im Hintergrund ----------

class BridgeServer:
    """Webserver und Spotify-/Geräteschleifen der Brücke in einem eigenen Thread mit eigener asyncio-Schleife."""

    def __init__(self) -> None:
        self.loop: asyncio.AbstractEventLoop | None = None
        self.error: Exception | None = None
        self.ready = threading.Event()
        self._runner = None

    def start(self) -> bool:
        threading.Thread(target=self._run, name="Bridge", daemon=True).start()
        if not self.ready.wait(15):
            self.error = TimeoutError("Die Brücke ist nicht rechtzeitig gestartet")
        return self.error is None

    def _run(self) -> None:
        loop = self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self._serve())
        except Exception as err:  # z. B. Port belegt
            log.exception("Brücke startet nicht")
            self.error = err
            self.ready.set()
            return
        self.ready.set()
        try:
            loop.run_forever()
        finally:
            loop.run_until_complete(self._runner.cleanup())
            loop.close()

    async def _serve(self) -> None:
        from aiohttp import web

        import bridge

        self._runner = web.AppRunner(bridge.create_app(), access_log=None)
        await self._runner.setup()
        await web.TCPSite(self._runner, bridge.HOST, bridge.PORT).start()
        log.info("Brücke läuft auf http://%s:%d", bridge.HOST, bridge.PORT)

    def stop(self) -> None:
        if self.loop and self.loop.is_running():
            self.loop.call_soon_threadsafe(self.loop.stop)


# ---------- Updates ----------

def ssl_context() -> ssl.SSLContext:
    # Windows-Zertifikatsspeicher: Virenscanner mit TLS-Prüfung brechen sonst jede Verbindung ab
    try:
        import truststore

        return truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    except Exception:
        return ssl.create_default_context()


def version_tuple(value: str) -> tuple:
    return tuple(int(x) for x in re.findall(r"\d+", value or "0")[:3])


# ---------- Schnittstelle für die Seite (window.pywebview.api) ----------

class Api:
    def __init__(self, settings: dict, quit_app) -> None:
        self._settings = settings
        self._quit_app = quit_app
        self._window = None
        self._update: dict | None = None
        self._installing = False

    def app_info(self) -> dict:
        return {
            "version": VERSION,
            "frozen": INSTALLED,
            "close_to_tray": bool(self._settings["close_to_tray"]),
            "autostart": is_autostart() if INSTALLED else bool(self._settings["autostart"]),
            "updates_configured": bool(UPDATE_URL),
            "data_dir": str(DATA_DIR),
            "language": self._settings.get("language", "auto"),
            "pages": paths.device_pages(self._settings),
            "audio_layout": paths.audio_layout(self._settings),
        }

    def set_setting(self, key: str, value) -> bool:
        if key == "language":
            if value not in i18n.CHOICES:
                return False
            self._settings["language"] = value
            save_settings(self._settings)
            return True
        if key == "pages":
            if not isinstance(value, dict):
                return False
            pages = {k: bool(value.get(k, v)) for k, v in paths.DEFAULT_PAGES.items()}
            if not any(pages.values()):
                return False  # eine Seite bleibt immer
            self._settings["pages"] = pages
            save_settings(self._settings)
            return True
        if key == "audio_layout":
            if value not in paths.AUDIO_LAYOUTS:
                return False
            self._settings["audio_layout"] = value
            save_settings(self._settings)
            return True
        if key not in ("close_to_tray", "autostart"):
            return False
        self._settings[key] = bool(value)
        save_settings(self._settings)
        if key == "autostart":
            set_autostart(bool(value))
        return True

    def pick_file(self, kind: str):
        import webview

        types = {
            "program": (t("Programme (*.exe;*.lnk;*.bat;*.cmd)"), t("Alle Dateien (*.*)")),
            "script": (t("Skripte (*.ps1;*.bat;*.cmd;*.py)"), t("Alle Dateien (*.*)")),
        }.get(kind, (t("Alle Dateien (*.*)"),))
        result = self._window.create_file_dialog(webview.FileDialog.OPEN, file_types=types)
        return result[0] if result else None

    def open_url(self, url: str) -> bool:
        if not re.match(r"^https?://", str(url)):
            return False
        webbrowser.open(url)
        return True

    def open_data_dir(self) -> None:
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        os.startfile(DATA_DIR)

    def quit(self) -> None:
        threading.Thread(target=self._quit_app, daemon=True).start()

    def check_update(self) -> dict:
        if not UPDATE_URL:
            return {"available": False, "current": VERSION, "configured": False}
        try:
            req = urllib.request.Request(UPDATE_URL, headers={"User-Agent": APP_ID})
            with urllib.request.urlopen(req, timeout=8, context=ssl_context()) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except Exception as err:
            return {"available": False, "current": VERSION, "configured": True,
                    "error": f"{type(err).__name__}: {str(err)[:120]}"}
        self._update = data
        return {"available": version_tuple(data.get("version")) > version_tuple(VERSION), "configured": True,
                "current": VERSION, "latest": data.get("version"), "notes": data.get("notes", "")}

    def install_update(self) -> dict:
        """Installer laden, Prüfsumme prüfen, Updater starten, App beenden (Updater + Installer machen den Rest)."""
        data = self._update
        if not data or not data.get("installer_url"):
            return {"ok": False, "error": t("Keine Update-Information – bitte erst nach Updates suchen.")}
        if not INSTALLED:
            if RELEASES_URL:
                webbrowser.open(RELEASES_URL)
            return {"ok": False, "error": t("Im Entwicklungsmodus wird nicht aktualisiert.")}
        expected = str(data.get("installer_sha256") or "").lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            return {"ok": False, "error": t("Update ohne gültige Prüfsumme – abgebrochen.")}
        updater = Path(sys.executable).with_name("deck_updater.exe")
        if not updater.exists():
            return {"ok": False, "error": t("deck_updater.exe fehlt – bitte die App neu installieren.")}
        if self._installing:
            return {"ok": False, "error": t("Update läuft bereits.")}
        self._installing = True
        setup = Path(tempfile.gettempdir()) / f"{APP_ID}-Setup.exe"
        part = setup.with_suffix(".part")
        try:
            req = urllib.request.Request(data["installer_url"], headers={"User-Agent": APP_ID})
            digest = hashlib.sha256()
            with urllib.request.urlopen(req, timeout=120, context=ssl_context()) as resp, open(part, "wb") as out:
                total = int(resp.headers.get("Content-Length") or 0)
                done, last = 0, -1
                while chunk := resp.read(65536):
                    out.write(chunk)
                    digest.update(chunk)
                    done += len(chunk)
                    percent = done * 100 // total if total else -1
                    if percent != last and self._window:
                        last = percent
                        self._window.evaluate_js(f"window.updateProgress && updateProgress({percent})")
            if total and part.stat().st_size != total:
                raise ValueError(t("Download unvollständig"))
            if digest.hexdigest() != expected:
                raise ValueError(t("Prüfsumme stimmt nicht – Update abgebrochen"))
            part.replace(setup)
            subprocess.Popen([str(updater), "--install", str(setup)], creationflags=DETACHED, close_fds=True)
            log.info("Update %s: Updater gestartet, App beendet sich", data.get("version"))
            self.quit()
            return {"ok": True}
        except Exception as err:
            log.warning("Update fehlgeschlagen: %s", err)
            return {"ok": False, "error": str(err)}
        finally:
            self._installing = False
            try:
                part.unlink()
            except OSError:
                pass


# ---------- Infobereich ----------

class Tray:
    def __init__(self, on_open, on_quit) -> None:
        self._on_open = on_open
        self._on_quit = on_quit
        self.icon = None

    def start(self) -> bool:
        try:
            import pystray
            from PIL import Image

            image = Image.open(RES / "assets" / "deck_thing.ico")
        except Exception as err:
            log.warning("Symbol im Infobereich nicht möglich: %s", err)
            return False
        menu = pystray.Menu(
            pystray.MenuItem(lambda _item: t("Öffnen"), lambda: self._on_open(), default=True),
            pystray.MenuItem(lambda _item: t("Beenden"), lambda: self._on_quit()),
        )
        self.icon = pystray.Icon(APP_ID, icon=image, title=APP_TITLE, menu=menu)
        threading.Thread(target=self.icon.run, name="Tray", daemon=True).start()
        return True

    def stop(self) -> None:
        if self.icon:
            try:
                self.icon.stop()
            except Exception:
                pass


ERROR_PAGE = """<!doctype html><html lang="de"><meta charset="utf-8"><title>Deck Thing</title>
<body style="margin:0;background:#121212;color:#fff;font:15px/1.5 'Segoe UI',sans-serif;padding:32px">
<h2 style="margin:0 0 8px">Deck Thing kann nicht starten</h2>
<p style="color:#b3b3b3">{text}</p><p style="color:#6a6a6a;font-size:13px">{detail}</p></body></html>"""


def set_window_icon() -> None:
    """Eigenes Symbol in Titelleiste und Taskleiste. Die installierte App erbt es von der .exe;
    beim Start aus dem Quellcode zeigte das Fenster sonst das Python-Symbol."""
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    user32.LoadImageW.restype = ctypes.c_void_p
    user32.LoadImageW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, wintypes.UINT, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, ctypes.c_void_p]
    path = str(RES / "assets" / "deck_thing.ico")
    big = user32.LoadImageW(None, path, 1, 32, 32, 0x10)    # IMAGE_ICON, LR_LOADFROMFILE
    small = user32.LoadImageW(None, path, 1, 16, 16, 0x10)
    pid = os.getpid()
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _lparam):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            buf = ctypes.create_unicode_buffer(128)
            user32.GetWindowTextW(hwnd, buf, 128)
            if buf.value == APP_TITLE:
                found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    for hwnd in found:
        user32.SendMessageW(hwnd, 0x0080, 1, big)    # WM_SETICON, ICON_BIG
        user32.SendMessageW(hwnd, 0x0080, 0, small)  # ICON_SMALL


def main() -> None:
    setup_logging()
    try:
        # eigene Gruppe in der Taskleiste statt „Python“
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("juppeee.DeckThing")
    except (AttributeError, OSError):
        pass
    log.info("Deck Thing %s startet (%s)", VERSION, "App" if FROZEN else "Entwicklung")
    if not acquire_single_instance():
        if restore_existing_window():
            return
        time.sleep(1.5)  # alte Instanz beendet sich vielleicht gerade (Update)
        if not acquire_single_instance() and restore_existing_window():
            return

    import webview

    settings = load_settings()
    if INSTALLED and bool(settings["autostart"]) != is_autostart():
        set_autostart(bool(settings["autostart"]))

    server = BridgeServer()
    if not server.start():
        busy = isinstance(server.error, OSError) and getattr(server.error, "winerror", None) in (10048, 10013)
        text = ("Port 8765 ist schon belegt – läuft die Brücke noch in einem Terminal? Bitte dort beenden."
                if busy else "Die Hintergrunddienste ließen sich nicht starten.")
        webview.create_window(APP_TITLE, html=ERROR_PAGE.format(text=text, detail=server.error),
                              width=620, height=300, background_color="#121212")
        webview.start()
        return

    state = {"quitting": False, "visible": True}
    window = None

    def real_quit() -> None:
        if state["quitting"]:
            return
        state["quitting"] = True
        try:
            if window and not settings.get("maximized"):
                settings["win_w"], settings["win_h"] = int(window.width), int(window.height)
                save_settings(settings)
        except Exception:
            pass
        tray.stop()
        for win in list(webview.windows):
            win.destroy()

    def show_window() -> None:
        if not state["visible"]:
            state["visible"] = True
            try:
                window.evaluate_js("window.playSplash && playSplash()")  # Startanimation auch beim Öffnen aus dem Infobereich
            except Exception:
                pass
        window.show()
        window.restore()

    def request_quit() -> None:
        """Ungespeicherte Tasten-Änderungen? Dann erst fragen (im Fenster, nicht per Browserdialog)."""
        try:
            dirty = bool(window.evaluate_js("window.hasUnsaved ? hasUnsaved() : false"))
        except Exception:
            dirty = False
        if dirty:
            show_window()
            window.evaluate_js("window.requestQuit && requestQuit()")
        else:
            real_quit()

    api = Api(settings, real_quit)
    tray = Tray(lambda: threading.Thread(target=show_window, daemon=True).start(),
                lambda: threading.Thread(target=request_quit, daemon=True).start())
    tray_ok = tray.start()

    import bridge

    start_hidden = "--tray" in sys.argv and tray_ok
    window = webview.create_window(
        APP_TITLE, url=f"http://{bridge.HOST}:{bridge.PORT}/app" + ("?tray" if start_hidden else ""), js_api=api,
        width=int(settings["win_w"]), height=int(settings["win_h"]), min_size=(980, 660),
        background_color="#121212", hidden=start_hidden,
    )
    api._window = window
    state["visible"] = not start_hidden

    def on_closing():
        if state["quitting"]:
            return True
        if settings["close_to_tray"] and tray.icon is not None:
            state["visible"] = False
            threading.Thread(target=window.hide, daemon=True).start()
        else:
            # evaluate_js darf nicht im Fenster-Thread laufen, der gerade das Schließen meldet
            threading.Thread(target=request_quit, daemon=True).start()
        return False

    def on_loaded():
        threading.Thread(target=set_window_icon, daemon=True).start()
        # Windows gibt dem ersten Fenster eines Prozesses den Anzeigemodus des Starters mit
        # (z. B. versteckt aus einer Verknüpfung oder einem Skript) – ohne --tray immer zeigen
        if not start_hidden:
            threading.Thread(target=show_window, daemon=True).start()

    window.events.closing += on_closing
    window.events.loaded += on_loaded
    webview.start(private_mode=False, storage_path=str(DATA_DIR / "webview"))
    tray.stop()
    server.stop()
    log.info("Deck Thing beendet")


if __name__ == "__main__":
    main()
