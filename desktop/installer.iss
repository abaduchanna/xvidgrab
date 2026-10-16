; 3SVerse VideoGrabber setup — Inno Setup 6
; PrivilegesRequired=lowest -> per-user install into %LOCALAPPDATA%, no UAC
#define MyAppName "3SVerse VideoGrabber"
#define MyAppVersion "1.3.0"
#define MyAppPublisher "3S Verse"
#define MyAppExeName "VideoGrabber.exe"

[Setup]
AppId={{7C2F1A64-9B3E-4D58-8A17-2F5C90BE4D31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://3sverse.com
AppSupportURL=https://github.com/abaduchanna/3SVerse_VideoGrabber
DefaultDirName={localappdata}\3SVerse\VideoGrabber
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=LICENSE
OutputDir=Output
OutputBaseFilename=3SVerse_VideoGrabber_v{#MyAppVersion}_Setup
SetupIconFile=src\icon.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardImageFile=setup_sidebar.bmp
WizardSmallImageFile=setup_mark.bmp
WizardImageStretch=no
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "dist\VideoGrabber.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\yt-dlp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\yt-dlp-UNLICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; downloaded media and .part temp files stay in the user's save-to folders on purpose
Type: files; Name: "{app}\*.part"
