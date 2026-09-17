; Deck Thing - installer (Inno Setup 6)
; Per user, no admin rights, into %LOCALAPPDATA%\Programs\DeckThing.
; Never touches %USERPROFILE%\.deck-thing (keys, images, Spotify login) - not even on uninstall.
; Silent (used by the updater): DeckThing-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

#define MyAppName "Deck Thing"
#define MyAppVersion "0.1.1"
#define MyAppPublisher "juppeee"
#define MyAppURL "https://github.com/juppeee/deck-thing"
#define MyAppExeName "DeckThing.exe"

[Setup]
; Stable GUID - must stay the same across all versions, otherwise installs pile up side by side
AppId={{6F3B8D21-4C7E-4A59-9E12-8B5D0C3A7F64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion={#MyAppVersion}
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\DeckThing
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=dist
OutputBaseFilename=DeckThing-Setup
SetupIconFile=assets\deck_thing.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no
; Close only the app - not the updater that is running this very update
CloseApplicationsFilter={#MyAppExeName}

; English only, like the rest of the project's outside; the app itself follows the Windows language
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "dist\DeckThing\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\DeckThing\_internal\*"; DestDir: "{app}\_internal"; Flags: ignoreversion recursesubdirs createallsubdirs
; restartreplace: if an old updater from this folder is running, replace it on the next reboot
Source: "dist\deck_updater.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace

[InstallDelete]
; remove old program files first so nothing from an earlier version gets loaded
Type: filesandordirs; Name: "{app}\_internal"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; start after a silent update too (no skipifsilent)
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall

[UninstallDelete]
Type: filesandordirs; Name: "{app}\_internal"

[Registry]
; the app writes its own autostart entry (Settings); remove it on uninstall
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "DeckThing"; Flags: uninsdeletevalue dontcreatekey
