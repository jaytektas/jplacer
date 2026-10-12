; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>
;
; jplacer's Windows installer (Inno Setup), made by packaging/build-windows.sh from the folder it gathers:
;   iscc /DVersion=<version> /DSource=<the gathered jplacer folder> /DOutput=<where the installer goes> jplacer.iss
; One AppId for every version, so each installs over the one before (JFramework's updater runs the new one with
; /SILENT once jplacer has closed, and its [Run] starts jplacer again). A person's own files (settings, cells, the
; library, in %APPDATA%\jplacer) are jplacer's, not the installer's: never touched, and left on uninstalling.

#ifndef Version
  #error Version not given (iscc /DVersion=...)
#endif
#ifndef Source
  #error Source not given (iscc /DSource=...)
#endif
#ifndef Output
  #define Output "."
#endif

[Setup]
AppId={{74863D7B-7785-41BF-AB2E-529ACF3789EB}
AppName=jplacer
AppVersion={#Version}
AppVerName=jplacer {#Version}
AppPublisher=Jaytek
AppPublisherURL=https://github.com/jaytektas/jplacer
AppSupportURL=https://github.com/jaytektas/jplacer/issues
AppUpdatesURL=https://github.com/jaytektas/jplacer/releases
DefaultDirName={autopf}\jplacer
DefaultGroupName=jplacer
DisableProgramGroupPage=yes
LicenseFile={#Source}\LICENSE
OutputDir={#Output}
OutputBaseFilename=jplacer-{#Version}-setup
SetupIconFile={#Source}\..\jplacer.ico
UninstallDisplayIcon={app}\jplacer.exe
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
; A jplacer still running is closed first (and the updater's has already closed itself).
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; What an older version installed and this one does not have (a renamed page of the manual, say): gone, so the
; folder is this version's alone.
Type: filesandordirs; Name: "{app}\manual"
Type: filesandordirs; Name: "{app}\profiles"
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\icons"
Type: filesandordirs; Name: "{app}\illustrations"
Type: filesandordirs; Name: "{app}\openscad"
Type: filesandordirs; Name: "{app}\openpnp-defaults"
Type: filesandordirs; Name: "{app}\licenses"

[Icons]
Name: "{group}\jplacer"; Filename: "{app}\jplacer.exe"
Name: "{group}\{cm:UninstallProgram,jplacer}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\jplacer"; Filename: "{app}\jplacer.exe"; Tasks: desktopicon

[Run]
; Started when it is done: offered after an install by hand, and done after the updater's silent one.
Filename: "{app}\jplacer.exe"; Description: "{cm:LaunchProgram,jplacer}"; Flags: nowait postinstall
