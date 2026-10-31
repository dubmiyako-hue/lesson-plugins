; Inno Setup script for the Lesson EQ / Lesson Comp installer. Built by tools\package-exe.py.
; The installer needs administrator rights: the VST3 folder is under Program Files.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Root
  #define Root "..\.."
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif
#ifndef LicDir
  #define LicDir "..\..\dist\licenses"
#endif

[Setup]
AppId={{6F1C2B7A-4D0E-4B8E-9A55-5C1E0D3A7B21}
AppName=Lesson EQ and Lesson Comp
AppVersion={#AppVersion}
AppPublisher=Personal
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=LessonPlugins-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
UninstallFilesDir={commonappdata}\Lesson Plugins
UsePreviousAppDir=no

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
Source: "{#Root}\build-rel\plugin\LessonEQ_artefacts\Release\VST3\Lesson EQ.vst3\*"; DestDir: "{commoncf64}\VST3\Lesson EQ.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Root}\build-rel\plugin\LessonComp_artefacts\Release\VST3\Lesson Comp.vst3\*"; DestDir: "{commoncf64}\VST3\Lesson Comp.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#LicDir}\*"; DestDir: "{commonappdata}\Lesson Plugins\licenses"; Flags: recursesubdirs ignoreversion

[Code]
function VcRuntimeInstalled: Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1);
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if not VcRuntimeInstalled then
    if MsgBox('Microsoft Visual C++ Redistributable 2015-2022 (x64) was not found. Without it a DAW cannot load the plug-ins. Continue anyway?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
end;
