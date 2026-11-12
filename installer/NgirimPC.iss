; Installer NgirimPC (Inno Setup 6). Dibangun CI: iscc /DMyVersion=2.9.0 /DMyExe=NgirimPC-v2.9.0.exe /DMyOut=NgirimPC-Setup-v2.9.0
; Per-pengguna (tanpa admin) di %LOCALAPPDATA%\Programs: folder bisa ditulis, jadi update otomatis tetap jalan.
#ifndef MyVersion
  #define MyVersion "0.0.0"
#endif
#ifndef MyExe
  #define MyExe "NgirimPC.exe"
#endif
#ifndef MyOut
  #define MyOut "NgirimPC-Setup"
#endif

[Setup]
AppId={{6E1B5C52-5B0E-4C6C-9A0B-4E67A1D0B7C3}
AppName=Ngirim PC
AppVersion={#MyVersion}
AppPublisher=UdyneO2
AppPublisherURL=https://github.com/UdyneO2/NgirimPC
DefaultDirName={localappdata}\Programs\NgirimPC
DefaultGroupName=Ngirim PC
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename={#MyOut}
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\NgirimPC.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
; nama tetap NgirimPC.exe di folder instalasi; updater mengganti file ini di tempat
Source: "..\{#MyExe}"; DestDir: "{app}"; DestName: "NgirimPC.exe"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Ngirim PC"; Filename: "{app}\NgirimPC.exe"
Name: "{autodesktop}\Ngirim PC"; Filename: "{app}\NgirimPC.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\NgirimPC.exe"; Description: "{cm:LaunchProgram,Ngirim PC}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; sisa update yang gagal
Type: files; Name: "{app}\NgirimPC.exe.old"
Type: files; Name: "{app}\NgirimPC.exe.new"
