; BayerFlow Windows Installer — Inno Setup Script
; Matches Mac notarization/packaging level

#define MyAppName "BayerFlow"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "BayerFlow"
#define MyAppURL "https://bayerflow.com"
#define MyAppExeName "bayerflow_qml.exe"
#define DeployDir "C:\Users\kaden\BayerFlow-Win\deploy"

[Setup]
AppId={{B4Y3RF10W-W1N-2026-0316-INSTALLER}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=
OutputDir=C:\Users\kaden\BayerFlow-Win
OutputBaseFilename=BayerFlow-Win-Setup-{#MyAppVersion}
SetupIconFile={#DeployDir}\icon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executable
Source: "{#DeployDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; App icon
Source: "{#DeployDir}\icon.ico"; DestDir: "{app}"; Flags: ignoreversion

; All DLLs in deploy root
Source: "{#DeployDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Qt plugins and QML modules
Source: "{#DeployDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\qmltooling\*"; DestDir: "{app}\qmltooling"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs
; QML files (app-specific)
Source: "{#DeployDir}\qml\*"; DestDir: "{app}\qml"; Flags: ignoreversion recursesubdirs createallsubdirs

; Model files
Source: "{#DeployDir}\raft_small.onnx"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\dncnn.onnx"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\postfilter_1ch_weights.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\S1M2_hotpixels.bin"; DestDir: "{app}"; Flags: ignoreversion

; VC++ Redistributable (downloaded separately)
Source: "{#DeployDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\icon.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\icon.ico"

[Run]
; Install VC++ Redistributable silently if bundled
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ 2022 Redistributable..."; Flags: waituntilterminated; Check: NeedsVCredist
; Launch app after install
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
; File association for .mov files (optional)
Root: HKCR; Subkey: ".mov\OpenWithProgids"; ValueType: string; ValueName: "BayerFlow.mov"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "BayerFlow.mov"; ValueType: string; ValueName: ""; ValueData: "ProRes RAW Video"; Flags: uninsdeletekey
Root: HKCR; Subkey: "BayerFlow.mov\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Code]
function NeedsVCredist: Boolean;
begin
  Result := not RegKeyExists(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64');
end;
