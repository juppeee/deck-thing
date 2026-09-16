"""Lautstärkemixer für die Audio-Seite des Geräts: Gesamt, Mikrofon, jede App und das Ausgabegerät.

Liest dasselbe wie der Windows-Lautstärkemixer (Core Audio über pycaw). pycaw spricht COM, deshalb
läuft alles in einem eigenen Thread mit CoInitialize. Programmsymbole holt Windows aus der .exe.
Das Umschalten des Ausgabegeräts geht über IPolicyConfig – nicht offiziell dokumentiert, aber seit
Windows 7 unverändert und genauso von EarTrumpet und SoundSwitch benutzt.
"""

from __future__ import annotations

import asyncio
import ctypes
import hashlib
import logging
import re
import time
import warnings
from concurrent.futures import ThreadPoolExecutor
from ctypes import wintypes

from PIL import Image

from i18n import t
from lvgl_image import rgb565a8

log = logging.getLogger("audio")

ICON_SIZE = 44
RECENT_SOUND_SECONDS = 20  # so lange zählt eine App als „spielt“ und steht vorn
SESSIONS_SECONDS = 2.0     # neue Apps so oft einsammeln; Pegel und Lautstärken jedes Mal
DEVICES_SECONDS = 10.0     # Geräteliste seltener: das dauert eine halbe Sekunde, die Pegel stocken sonst
SILENT_PEAK = 0.002

_user32 = ctypes.windll.user32
_gdi32 = ctypes.windll.gdi32
_version = ctypes.windll.version
_user32.PrivateExtractIconsW.argtypes = [wintypes.LPCWSTR, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p, wintypes.UINT, wintypes.UINT]
_user32.GetDC.restype = ctypes.c_void_p
_user32.GetDC.argtypes = [ctypes.c_void_p]
_user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_user32.DrawIconEx.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                               wintypes.UINT, ctypes.c_void_p, wintypes.UINT]
_user32.DestroyIcon.argtypes = [ctypes.c_void_p]
_gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
_gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
_gdi32.CreateDIBSection.restype = ctypes.c_void_p
_gdi32.CreateDIBSection.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT, ctypes.POINTER(ctypes.c_void_p),
                                    ctypes.c_void_p, wintypes.DWORD]
_gdi32.SelectObject.restype = ctypes.c_void_p
_gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
_gdi32.DeleteDC.argtypes = [ctypes.c_void_p]


class _BitmapInfoHeader(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD)]


def short_id(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8", "replace")).hexdigest()[:16]


def exe_icon(path: str, size: int = ICON_SIZE) -> Image.Image | None:
    """Programmsymbol der .exe in der gewünschten Größe, freigestellt (RGBA)."""
    hicon = ctypes.c_void_p()
    if _user32.PrivateExtractIconsW(path, 0, size, size, ctypes.byref(hicon), None, 1, 0) <= 0 or not hicon.value:
        return None
    screen = _user32.GetDC(None)
    dc = _gdi32.CreateCompatibleDC(screen)
    header = _BitmapInfoHeader(ctypes.sizeof(_BitmapInfoHeader), size, -size, 1, 32, 0, 0, 0, 0, 0, 0)
    bits = ctypes.c_void_p()
    bitmap = _gdi32.CreateDIBSection(dc, ctypes.byref(header), 0, ctypes.byref(bits), None, 0)
    try:
        if not bitmap:
            return None
        old = _gdi32.SelectObject(dc, bitmap)
        _user32.DrawIconEx(dc, 0, 0, hicon, size, size, 0, None, 3)  # DI_NORMAL
        raw = ctypes.string_at(bits, size * size * 4)
        _gdi32.SelectObject(dc, old)
    finally:
        if bitmap:
            _gdi32.DeleteObject(bitmap)
        _gdi32.DeleteDC(dc)
        _user32.ReleaseDC(None, screen)
        _user32.DestroyIcon(hicon)
    img = Image.frombuffer("RGBA", (size, size), raw, "raw", "BGRA", 0, 1).copy()
    alpha = img.getchannel("A")
    if not alpha.getbbox():
        img.putalpha(255)  # altes Symbol ohne Alphakanal
        return img
    # DrawIconEx liefert vormultiplizierte Farben – zurückrechnen, sonst werden Kanten dunkel
    px = img.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            if 0 < a < 255:
                px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
    return img


def icon_color(img: Image.Image) -> str:
    """Kräftige Grundfarbe des Symbols für die getönte Kachel."""
    small = img.resize((12, 12), Image.Resampling.BOX)
    best, best_score = (180, 180, 180), -1.0
    for r, g, b, a in small.getdata():
        if a < 160:
            continue
        hi, lo = max(r, g, b), min(r, g, b)
        score = (hi - lo) + hi * 0.25
        if score > best_score:
            best, best_score = (r, g, b), score
    return "#%02X%02X%02X" % best


def file_description(path: str) -> str | None:
    """„Beschreibung“ aus den Dateieigenschaften, z. B. „Google Chrome“ statt chrome.exe."""
    try:
        size = _version.GetFileVersionInfoSizeW(path, None)
        if not size:
            return None
        data = ctypes.create_string_buffer(size)
        if not _version.GetFileVersionInfoW(path, 0, size, data):
            return None
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        if not _version.VerQueryValueW(data, "\\VarFileInfo\\Translation", ctypes.byref(pointer), ctypes.byref(length)):
            return None
        lang, codepage = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint16 * 2)).contents
        key = f"\\StringFileInfo\\{lang:04x}{codepage:04x}\\FileDescription"
        if not _version.VerQueryValueW(data, key, ctypes.byref(pointer), ctypes.byref(length)) or not length.value:
            return None
        text = ctypes.wstring_at(pointer).strip()  # bis zur Null; length zählt bei manchen Dateien falsch
        return text or None
    except OSError:
        return None


def device_name(friendly: str | None) -> str:
    """„Lautsprecher (2- USB Audio Device)“ → „Lautsprecher (USB Audio Device)“ – die Nummer vergibt Windows
    nur zur Unterscheidung gleicher Treiber und sagt niemandem etwas."""
    return re.sub(r"\((\d+)- ", "(", friendly or "").strip()


class AudioMixer:
    def __init__(self) -> None:
        self._pool = ThreadPoolExecutor(max_workers=1, initializer=self._init_com)
        self.icons: dict[str, bytes] = {}      # Kennung → RGB565A8 mit Breite/Höhe davor
        self._app_info: dict[str, tuple[str, str | None, str]] = {}  # exe → (Name, icon_id, Farbe)
        self._last_sound: dict[str, float] = {}
        self._outputs: dict[str, str] = {}     # kurze Kennung → Windows-Geräte-ID
        self._output_list: list[dict] = []
        self._apps: dict[str, dict] = {}       # App-Kennung → Name, Symbol, Sitzungen und Pegelmesser
        self._out = self._mic = None
        self._devices_read = self._sessions_read = 0.0
        self._dirty = True

    @staticmethod
    def _init_com() -> None:
        import comtypes

        comtypes.CoInitialize()
        warnings.filterwarnings("ignore", module="pycaw")  # Geräte ohne manche Eigenschaften melden sich laut

    async def _run(self, fn, *args):
        return await asyncio.get_running_loop().run_in_executor(self._pool, fn, *args)

    # ---------- Lesen ----------

    @staticmethod
    def _meter(device):
        from comtypes import CLSCTX_ALL
        from pycaw.pycaw import IAudioMeterInformation

        # QueryInterface statt ctypes.cast: cast zählt die Referenz nicht mit, das doppelte Release stürzt ab
        return device.Activate(IAudioMeterInformation._iid_, CLSCTX_ALL, None).QueryInterface(IAudioMeterInformation)

    @staticmethod
    def _endpoint_volume(device):
        from comtypes import CLSCTX_ALL
        from pycaw.pycaw import IAudioEndpointVolume

        return device.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None).QueryInterface(IAudioEndpointVolume)

    def _app(self, exe: str | None, display: str | None, process_name: str | None) -> tuple[str, str | None, str]:
        key = exe or process_name or display or "?"
        if key in self._app_info:
            return self._app_info[key]
        name = (file_description(exe) if exe else None) or (display if display and not display.startswith("@") else None)
        if not name and process_name:
            name = process_name.rsplit(".", 1)[0].replace("_", " ").title()
        icon_id, color = None, "#6A6A6A"
        if exe:
            try:
                img = exe_icon(exe)
            except Exception:
                img = None
            if img is not None:
                icon_id = short_id("icon:" + exe.lower())
                self.icons[icon_id] = ICON_SIZE.to_bytes(2, "little") * 2 + rgb565a8(img)
                color = icon_color(img)
        info = (name or "?", icon_id, color)
        self._app_info[key] = info
        return info

    def _refresh_devices(self) -> None:
        """Ausgabegerät, Mikrofon und Geräteliste (langsam: Windows fragt jedes Gerät einzeln ab)."""
        from pycaw.pycaw import AudioUtilities

        self._out = None
        speakers = AudioUtilities.GetSpeakers()
        default_id = None
        if speakers is not None:
            default_id = speakers.id
            self._out = {"id": "out", "name": t("Gesamt"), "sub": device_name(speakers.FriendlyName), "color": "#1ED760",
                         "volume": speakers.EndpointVolume, "meter": self._meter(speakers._dev)}

        self._mic = None
        mic = AudioUtilities.GetMicrophone()
        if mic is not None:
            self._mic = {"id": "mic", "name": t("Mikrofon"), "sub": device_name(AudioUtilities.CreateDevice(mic).FriendlyName),
                         "color": "#8EC5E8", "volume": self._endpoint_volume(mic), "meter": self._meter(mic)}

        self._outputs = {}
        self._output_list = []
        for device in AudioUtilities.GetAllDevices():
            if str(device.state).endswith("Active") and AudioUtilities.GetEndpointDataFlow(device.id) == "eRender":
                oid = short_id(device.id)
                self._outputs[oid] = device.id
                self._output_list.append({"id": oid, "name": device_name(device.FriendlyName), "active": device.id == default_id})
        self._devices_read = time.monotonic()

    def _refresh_sessions(self) -> None:
        """Welche Apps gerade eine Audio-Sitzung haben (schnell genug für alle zwei Sekunden)."""
        from pycaw.pycaw import AudioUtilities, IAudioMeterInformation

        apps: dict[str, dict] = {}
        for session in AudioUtilities.GetAllSessions():
            if session.State == 2:  # abgelaufen
                continue
            proc = session.Process
            display = session.DisplayName or ""
            if proc is None:
                if not display.startswith("@%SystemRoot%"):
                    continue
                key, name, icon_id, color = "system", t("Systemklänge"), None, "#6A6A6A"
            else:
                try:
                    exe, pname = proc.exe(), proc.name()
                except Exception:
                    continue
                key = (exe or pname).lower()
                name, icon_id, color = self._app(exe, display, pname)
            aid = short_id("app:" + key)
            entry = apps.setdefault(aid, {"id": aid, "name": name, "sub": "", "icon_id": icon_id or "", "color": color,
                                          "volumes": [], "meters": [], "system": key == "system"})
            entry["volumes"].append(session.SimpleAudioVolume)
            entry["meters"].append(session._ctl.QueryInterface(IAudioMeterInformation))
        self._apps = apps
        self._sessions_read = time.monotonic()

    def _snapshot(self) -> dict:
        """Lautstärken und Pegel aus den gemerkten Sitzungen (schnell, für zehn Bilder pro Sekunde)."""
        now = time.monotonic()
        if self._dirty or now - self._devices_read > DEVICES_SECONDS:
            self._refresh_devices()
        if self._dirty or now - self._sessions_read > SESSIONS_SECONDS:
            self._refresh_sessions()
        self._dirty = False

        def endpoint(ch):
            if ch is None:
                return None
            try:
                return {"id": ch["id"], "name": ch["name"], "sub": ch["sub"], "color": ch["color"],
                        "vol": round(ch["volume"].GetMasterVolumeLevelScalar() * 100), "muted": bool(ch["volume"].GetMute()),
                        "level": round(ch["meter"].GetPeakValue() * 100)}
            except Exception:
                self._dirty = True  # Gerät weg – beim nächsten Mal neu einsammeln
                return None

        apps = []
        for app in self._apps.values():
            try:
                peak = max(m.GetPeakValue() for m in app["meters"])
                vol = app["volumes"][0]
                volume, muted = round(vol.GetMasterVolume() * 100), bool(vol.GetMute())
            except Exception:
                self._dirty = True  # Sitzung beendet
                continue
            if peak > SILENT_PEAK:
                self._last_sound[app["id"]] = now
            apps.append({k: app[k] for k in ("id", "name", "sub", "icon_id", "color")}
                        | {"vol": volume, "muted": muted, "level": round(peak * 100), "system": app["system"]})

        def order(app):
            recent = now - self._last_sound.get(app["id"], -1e9) < RECENT_SOUND_SECONDS
            return (not recent, app.pop("system"), app["name"].lower())

        apps.sort(key=order)
        return {"out": endpoint(self._out), "mic": endpoint(self._mic), "apps": apps, "outputs": self._output_list}

    async def snapshot(self) -> dict:
        return await self._run(self._snapshot)

    # ---------- Steuern ----------

    def _set_volume(self, channel: str, percent: int) -> None:
        from pycaw.pycaw import AudioUtilities

        level = max(0, min(100, int(percent))) / 100
        if channel == "out":
            vol = AudioUtilities.GetSpeakers().EndpointVolume
            vol.SetMasterVolumeLevelScalar(level, None)
            vol.SetMute(0, None)
        elif channel == "mic":
            vol = self._endpoint_volume(AudioUtilities.GetMicrophone())
            vol.SetMasterVolumeLevelScalar(level, None)
            vol.SetMute(0, None)
        else:
            for vol in self._apps.get(channel, {}).get("volumes", []):
                vol.SetMasterVolume(level, None)
                vol.SetMute(0, None)

    def _set_mute(self, channel: str, muted: bool) -> None:
        from pycaw.pycaw import AudioUtilities

        if channel == "out":
            AudioUtilities.GetSpeakers().EndpointVolume.SetMute(1 if muted else 0, None)
        elif channel == "mic":
            self._endpoint_volume(AudioUtilities.GetMicrophone()).SetMute(1 if muted else 0, None)
        else:
            for vol in self._apps.get(channel, {}).get("volumes", []):
                vol.SetMute(1 if muted else 0, None)

    def _set_output(self, output: str) -> None:
        from pycaw.constants import ERole
        from pycaw.pycaw import AudioUtilities

        device_id = self._outputs.get(output)
        if device_id is None:
            raise ValueError("unbekanntes Ausgabegerät")
        # wie die Auswahl in Windows: für Medien, Spiele und Anrufe zugleich
        AudioUtilities.SetDefaultDevice(device_id, [ERole.eConsole, ERole.eMultimedia, ERole.eCommunications])
        self._dirty = True

    async def set_volume(self, channel: str, percent: int) -> None:
        await self._run(self._set_volume, channel, percent)

    async def set_mute(self, channel: str, muted: bool) -> None:
        await self._run(self._set_mute, channel, muted)

    async def set_output(self, output: str) -> None:
        await self._run(self._set_output, output)
