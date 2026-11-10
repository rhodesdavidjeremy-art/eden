; Inno Setup script for Eden Research Reader. Run by .github/workflows/windows.yml after dist\Eden is assembled.
[Setup]
AppName=Eden Research Reader
AppVersion=0.1.0
AppPublisher=Eden
DefaultDirName={autopf}\Eden Research Reader
DefaultGroupName=Eden Research Reader
OutputDir=..\..\dist-installer
OutputBaseFilename=EdenReader-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
WizardStyle=modern
UninstallDisplayIcon={app}\eden_reader.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\..\dist\Eden\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Eden Research Reader"; Filename: "{app}\eden_reader.exe"
Name: "{autodesktop}\Eden Research Reader"; Filename: "{app}\eden_reader.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\eden_reader.exe"; Description: "Launch Eden Research Reader"; Flags: nowait postinstall skipifsilent
