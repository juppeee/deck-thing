@echo off
REM Deck Thing - build the Windows app
REM ----------------------------------
REM   1. PyInstaller onedir build (fast start, nothing unpacked to TEMP)
REM   2. The separate updater as a single exe
REM   3. Inno Setup installer (per user, %LOCALAPPDATA%\Programs\DeckThing)
REM
REM Run from this folder:  .\build.bat < NUL
REM Needs: pip install -r requirements.txt pyinstaller
REM        Inno Setup 6 (winget install JRSoftware.InnoSetup)

setlocal
cd /d "%~dp0"
set SETUPTOOLS_USE_DISTUTILS=stdlib
set PYTHONIOENCODING=utf-8
set ISCC="%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

echo [0/4] Checking translations...
python tools\collect_i18n.py
if errorlevel 1 (
  echo Untranslated interface text above - add it to tools\en_table.py and run it.
  goto :error
)

echo [1/4] Cleaning old builds...
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist

echo [2/4] App (onedir)...
pyinstaller --noconfirm --clean --onedir --noconsole ^
  --name DeckThing ^
  --icon assets\deck_thing.ico ^
  --add-data "web;web" ^
  --add-data "assets;assets" ^
  --collect-all winrt ^
  --exclude-module numpy ^
  --exclude-module pygame ^
  --exclude-module tkinter ^
  --exclude-module cryptography ^
  --exclude-module matplotlib ^
  --hidden-import pystray._win32 ^
  --hidden-import bridge ^
  --hidden-import spotify_api ^
  --hidden-import paths ^
  --hidden-import i18n ^
  --hidden-import audio_mixer ^
  --hidden-import lvgl_image ^
  deck_thing.py
if errorlevel 1 goto :error

echo [3/4] Updater...
pyinstaller --noconfirm --clean --onefile --noconsole --name deck_updater deck_updater.py
if errorlevel 1 goto :error

echo [4/4] Installer...
%ISCC% /Q setup.iss
if errorlevel 1 goto :error

echo.
echo Done: dist\DeckThing-Setup.exe
exit /b 0

:error
echo.
echo BUILD FAILED
exit /b 1
