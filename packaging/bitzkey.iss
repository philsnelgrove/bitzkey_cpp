; The Bitzkey installer, built by build_installer.ps1 with Inno Setup 6.
; It installs for the current user alone (no administrator rights needed), into
; %LOCALAPPDATA%\Programs\Bitzkey, with a Start menu shortcut and an uninstaller.
; Reports and exports go to Documents\Bitzkey, which uninstalling leaves alone.

#define AppName "Bitzkey"
#ifndef AppVersion
  ; build_installer.ps1 passes the version; otherwise it comes from the VERSION file
  #define VersionFile FileOpen(AddBackslash(SourcePath) + "..\VERSION")
  #define AppVersion Trim(FileRead(VersionFile))
  #expr FileClose(VersionFile)
#endif
#define Release "..\build\Release"
#define Deps "..\build\_deps"

[Setup]
AppId={{6B0F3C2E-8E57-4A3B-9C61-2B7D5E4F1A90}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Bitzkey-Setup-{#AppVersion}
SetupIconFile=bitzkey.ico
UninstallDisplayIcon={app}\bitzkey.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Release}\bitzkey.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\bitzkey_cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\SDL2_ttf.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\i18n\*.json"; DestDir: "{app}\i18n"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Deps}\sdl2-src\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "SDL2.txt"; Flags: ignoreversion
Source: "{#Deps}\sdl2_ttf-src\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "SDL2_ttf.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bitzkey.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bitzkey.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bitzkey.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
