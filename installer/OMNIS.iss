; =============================================================================
;  Abrasion Labs // OMNIS — installateur Windows (Inno Setup 6, gratuit)
;
;  Installe OMNIS.vst3 dans le dossier VST3 commun (C:\Program Files\Common
;  Files\VST3, celui que FL Studio et les autres hôtes lisent), et, au choix,
;  l'outil de mesure des références (omnis_refdb.exe). Aucun son, aucune image.
;
;  Compilation (CI, ou à la main) :
;    iscc /DAppVersion=0.1.0 /DSrcDir=..\build installer\OMNIS.iss
;  -> installer\Output\OMNIS-<version>-Windows.exe
; =============================================================================
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\build"
#endif

[Setup]
AppId={{6F0B8C52-4E1D-4C8A-9B3E-0A6D2F7C1E55}
AppName=OMNIS
AppVersion={#AppVersion}
AppPublisher=Abrasion Labs
DefaultDirName={autopf}\Abrasion Labs\OMNIS
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=OMNIS-{#AppVersion}-Windows
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
InfoBeforeFile=..\LICENSE_NOTICES.md
UninstallDisplayName=OMNIS (Abrasion Labs)

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "{cm:FullInstall}"
Name: "plugin"; Description: "{cm:PluginOnly}"

[Components]
Name: "plugin"; Description: "{cm:CompPlugin}"; Types: full plugin; Flags: fixed
Name: "tools"; Description: "{cm:CompTools}"; Types: full

[CustomMessages]
fr.FullInstall=Plug-in et outil de mesure des références
fr.PluginOnly=Plug-in seulement
fr.CompPlugin=OMNIS (VST3) — dans le dossier VST3 commun
fr.CompTools=Outil de mesure des références (omnis_refdb)
en.FullInstall=Plug-in and reference measuring tool
en.PluginOnly=Plug-in only
en.CompPlugin=OMNIS (VST3) — in the common VST3 folder
en.CompTools=Reference measuring tool (omnis_refdb)

[Files]
; Le paquet VST3 (un dossier) va dans le dossier VST3 commun, là où les hôtes le cherchent.
Source: "{#SrcDir}\VST3\Release\OMNIS.vst3\*"; DestDir: "{commoncf64}\VST3\OMNIS.vst3"; Components: plugin; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE_NOTICES.md"; DestDir: "{app}"; Components: plugin; Flags: ignoreversion
Source: "{#SrcDir}\bin\Release\omnis_refdb.exe"; DestDir: "{app}"; Components: tools; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\docs\REFERENCES.md"; DestDir: "{app}"; Components: tools; Flags: ignoreversion

[UninstallDelete]
; Le paquet du plug-in seulement : le dossier de l'utilisateur (cache, apprentissage) reste.
Type: filesandordirs; Name: "{commoncf64}\VST3\OMNIS.vst3"
