#define MyAppName "SubliminalCam"
#define MyAppVersion "0.7.0"
#define MyAppPublisher "SubliminalCam contributors"
#define MyAppExeName "SubliminalCam.exe"

[Setup]
AppId={{9D268F0C-8A61-4B9A-9778-7C7A029732F4}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\SubliminalCam
DefaultGroupName=SubliminalCam
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22621
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=SubliminalCamSetup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE

[Files]
Source: "..\out\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\SubliminalCam"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\SubliminalCam"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\SubliminalCamVcamCtl.exe"; Parameters: "install ""{app}\VirtualCameraMediaSource.dll"""; StatusMsg: "Registering the SubliminalCam virtual camera..."; Flags: runhidden waituntilterminated
Filename: "{app}\SubliminalCam.exe"; Description: "Launch SubliminalCam"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\SubliminalCamVcamCtl.exe"; Parameters: "remove"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVirtualCamera"
