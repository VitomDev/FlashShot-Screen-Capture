; FlashShot Screen Capture installer (Inno Setup 6).
;
; Per-user install without administrator rights. "Start with Windows" is
; enabled on a fresh install only; upgrades keep whatever the user chose.
; Uninstalling removes the startup entry and the settings.
;
; Built by build.ps1, which passes /DBuildDir=<folder with FlashShot.exe>.

#define AppVersion "1.0.1"
#define AppName "FlashShot Screen Capture"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define StartupApprovedKey "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"

#ifndef BuildDir
  #define BuildDir "..\build\app"
#endif

[Setup]
AppId={{FFE067A3-21B6-42AD-8572-F42334E6DB65}
AppName={#AppName}
AppVerName={#AppName} {#AppVersion}
AppVersion={#AppVersion}
AppPublisher=VitomDev
AppPublisherURL=https://github.com/VitomDev/FlashShot-Screen-Capture
AppSupportURL=https://github.com/VitomDev/FlashShot-Screen-Capture/issues
AppCopyright=Copyright (c) 2026 VitomDev
VersionInfoVersion={#AppVersion}
VersionInfoCompany=VitomDev
VersionInfoDescription={#AppName} - Capture and annotate the screen Setup
DefaultDirName={localappdata}\Programs\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=FlashShotSetup
SetupIconFile=..\assets\flashshot.ico
UninstallDisplayIcon={app}\FlashShot.exe
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
; FlashShot is closed by the code below, through its own window.
CloseApplications=no

[Files]
Source: "{#BuildDir}\FlashShot.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\FlashShot.exe"

[Run]
; The usual "Launch" checkbox on the last page, checked by default. FlashShot
; starts once Finish is clicked; silent upgrades start it as well.
Filename: "{app}\FlashShot.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\FlashShot.exe"; Flags: nowait; Check: WizardSilent

[Code]
var
  IsUpgrade: Boolean;

// FlashShot closes cleanly (tray icon, shortcuts) when its window gets WM_CLOSE.
procedure CloseRunningFlashShot;
var
  Window: HWND;
  Attempts: Integer;
begin
  Window := FindWindowByClassName('FlashShotScreenCaptureTray');
  if Window = 0 then Exit;
  PostMessage(Window, 16 { WM_CLOSE }, 0, 0);
  Attempts := 0;
  while (FindWindowByClassName('FlashShotScreenCaptureTray') <> 0) and (Attempts < 50) do
  begin
    Sleep(100);
    Attempts := Attempts + 1;
  end;
  // Give the process a moment to exit after its window is gone.
  Sleep(300);
end;

function InitializeSetup: Boolean;
begin
  IsUpgrade := RegKeyExists(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1');
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningFlashShot;
  Result := '';
end;

function AutostartCommand: String;
begin
  Result := '"' + ExpandConstant('{app}\FlashShot.exe') + '" --background';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not IsUpgrade then
      // Fresh install: Start with Windows is on by default.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand)
    else if RegValueExists(HKCU, '{#RunKey}', '{#AppName}') then
      // Upgrade: keep the user's choice, only refresh the path.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand);
  end;
end;

function InitializeUninstall: Boolean;
begin
  CloseRunningFlashShot;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKCU, '{#RunKey}', '{#AppName}');
    RegDeleteValue(HKCU, '{#StartupApprovedKey}', '{#AppName}');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\{#AppName}');
  end;
end;
