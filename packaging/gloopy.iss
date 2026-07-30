; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
; SPDX-License-Identifier: AGPL-3.0-only
;
; Inno Setup script for the Gloopy Windows installer (gloopy-setup-x64.exe).
; Packages the same self-contained folder the portable zip ships (exe + runtime
; DLLs + bundled assets + Surge XT VST3 + licenses) into a per-user installer that
; adds a Start-Menu shortcut, an optional PATH entry (for the `gloopy` CLI + MCP
; server), and a proper uninstaller. No admin rights required.
;
; Invoked from CI as:
;   ISCC /DAppVersion=<x.y.z> /DStageDir=<abs staging dir> /DSrcRoot=<abs repo root> packaging\gloopy.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "staging\gloopy-windows-x64"
#endif
#ifndef SrcRoot
  #define SrcRoot "."
#endif

#define MyAppName "Gloopy"
#define MyAppExe  "gloopy.exe"
#define MyAppPublisher "Anthony Green"
#define MyAppURL "https://atgreen.github.io/gloopy/"

[Setup]
AppId={{7C2B6E2A-9E2C-4A1B-9F3A-8D0B2E5C1A77}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={localappdata}\Programs\Gloopy
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile={#SrcRoot}\LICENSE
OutputDir={#SrcRoot}
OutputBaseFilename=gloopy-setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
ChangesEnvironment=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExe}
UninstallDisplayName={#MyAppName} {#AppVersion}

[Tasks]
Name: "addtopath"; Description: "Add Gloopy to my PATH (enables the ""gloopy"" command line and MCP server)"; GroupDescription: "Integration:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
; Append the install dir to the *user* PATH (only if not already present).
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath('{app}')

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  { true only if the dir isn't already on PATH (case-insensitive) }
  Result := Pos(';' + Lowercase(ExpandConstant(Param)) + ';',
                ';' + Lowercase(OrigPath) + ';') = 0;
end;
