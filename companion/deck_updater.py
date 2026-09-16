"""Separater Update-Prozess für Deck Thing (wie Chrome oder VS Code).

Ablauf:
1. App lädt den Installer, prüft SHA-256 und ruft: deck_updater.exe --install <setup.exe>
2. App beendet sich
3. Updater kopiert sich nach TEMP (seine eigene Datei soll ersetzt werden) und läuft dort weiter
4. Updater wartet, bis die App weg ist, beendet sie notfalls
5. Updater führt den Installer still aus; der Installer startet die App neu

Übernommen vom Claude Session Browser (csb_updater.py), dort über mehrere Versionen erprobt.
Bewusst ohne tasklist/taskkill: jeder Aufruf blitzt ein Konsolenfenster auf.
"""
import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

VERSION = "1.0.0"
APP_NAME = "DeckThing"
APP_EXE = "DeckThing.exe"

TH32CS_SNAPPROCESS = 0x00000002
PROCESS_TERMINATE = 0x0001
INVALID_HANDLE_VALUE = -1
DETACHED = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP


def log(msg):
    line = f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {msg}\n"
    try:
        with open(Path(os.environ.get("TEMP", ".")) / "deck_updater.log", "a", encoding="utf-8") as f:
            f.write(line)
    except OSError:
        pass


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.c_ulong), ("cntUsage", ctypes.c_ulong), ("th32ProcessID", ctypes.c_ulong),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)), ("th32ModuleID", ctypes.c_ulong),
        ("cntThreads", ctypes.c_ulong), ("th32ParentProcessID", ctypes.c_ulong),
        ("pcPriClassBase", ctypes.c_long), ("dwFlags", ctypes.c_ulong), ("szExeFile", ctypes.c_wchar * 260),
    ]


def pids_by_name(name):
    k32 = ctypes.windll.kernel32
    k32.CreateToolhelp32Snapshot.restype = ctypes.c_void_p
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if not snap or snap == ctypes.c_void_p(INVALID_HANDLE_VALUE).value:
        return []
    pids = []
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = k32.Process32FirstW(ctypes.c_void_p(snap), ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == name.lower():
                pids.append(entry.th32ProcessID)
            ok = k32.Process32NextW(ctypes.c_void_p(snap), ctypes.byref(entry))
    finally:
        k32.CloseHandle(ctypes.c_void_p(snap))
    return pids


def wait_until_gone(name, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if not pids_by_name(name):
            return True
        time.sleep(0.5)
    return False


def kill(name):
    k32 = ctypes.windll.kernel32
    for pid in pids_by_name(name):
        handle = k32.OpenProcess(PROCESS_TERMINATE, False, pid)
        if handle:
            k32.TerminateProcess(handle, 1)
            k32.CloseHandle(handle)


def install_dir():
    local = os.environ.get("LOCALAPPDATA", os.path.expanduser("~"))
    return Path(local) / "Programs" / APP_NAME


def relaunch_from_temp(setup):
    """Die eigene Datei liegt im Installationsordner und wäre beim Installieren gesperrt."""
    if not getattr(sys, "frozen", False):
        return False
    src = Path(sys.executable)
    dst = Path(os.environ.get("TEMP", ".")) / "deck_updater_run.exe"
    try:
        if src.resolve() == dst.resolve():
            return False
        shutil.copy2(src, dst)
        subprocess.Popen([str(dst), "--install", str(setup), "--relaunched"], creationflags=DETACHED, close_fds=True)
        log(f"weiter aus {dst}")
        return True
    except OSError as err:
        log(f"Kopie nach TEMP ging nicht ({err}), mache hier weiter")
        return False


def do_install(setup, relaunched):
    log(f"=== Deck Thing Updater {VERSION} === {setup}")
    if not os.path.exists(setup):
        log("Installer fehlt")
        return False
    if not relaunched and relaunch_from_temp(setup):
        return True
    if not wait_until_gone(APP_EXE, 10):
        log("App läuft noch, wird beendet")
        kill(APP_EXE)
        wait_until_gone(APP_EXE, 5)
    lock = Path(os.environ.get("LOCALAPPDATA", ".")) / f"{APP_NAME}.instance.lock"
    try:
        lock.unlink()
    except OSError:
        pass
    time.sleep(1)
    try:
        code = subprocess.run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCANCEL"],
                              timeout=300).returncode
    except (OSError, subprocess.TimeoutExpired) as err:
        log(f"Installer-Fehler: {err}")
        return False
    log(f"Installer beendet mit {code}")
    try:
        os.remove(setup)
    except OSError:
        pass
    # Der Installer startet die App selbst ([Run] + DeinitializeSetup); nur falls nicht:
    time.sleep(3)
    app = install_dir() / APP_EXE
    if code == 0 and app.exists() and not pids_by_name(APP_EXE):
        subprocess.Popen([str(app)], creationflags=DETACHED, close_fds=True)
        log("App gestartet")
    return code == 0


def main():
    parser = argparse.ArgumentParser(description="Deck Thing Updater")
    parser.add_argument("--install", metavar="SETUP")
    parser.add_argument("--relaunched", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.install:
        return 0 if do_install(args.install, args.relaunched) else 1
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
