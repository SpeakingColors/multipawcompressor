; Inno Setup script. Build: iscc /DMyVersion=1.2.3 /DSourceVst3=path\to\"MultiPaw Compressor.vst3" /DManualPdf=path\to\manual.pdf MultiPawCompressor.iss
#ifndef MyVersion
  #define MyVersion "0.0.0"
#endif
#ifndef SourceVst3
  #define SourceVst3 "..\..\build\MultiPawCompressor_artefacts\Release\VST3\MultiPaw Compressor.vst3"
#endif
[Setup]
AppId={{8B3D5C21-6A47-4E09-B1D2-3F5E7A9C0D14}
AppName=MultiPaw Compressor
AppVersion={#MyVersion}
AppPublisher=Studio 87
AppPublisherURL=https://github.com/SpeakingColors/multipawcompressor
DefaultDirName={autopf}\Studio 87\MultiPaw Compressor
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=MultiPawCompressor-Setup
Compression=lzma2
SolidCompression=yes
LicenseFile=..\..\LICENSE
UninstallDisplayName=MultiPaw Compressor
CloseApplications=yes
[Messages]
SelectDirDesc=Where should the manual and licence go?
SelectDirLabel3=The plugin itself is installed to the common VST3 folder (C:\Program Files\Common Files\VST3). This folder only holds the manual, the licence and the uninstaller.
[Files]
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\MultiPaw Compressor.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
#ifdef ManualPdf
Source: "{#ManualPdf}"; DestDir: "{app}"; DestName: "MultiPawCompressor-manual.pdf"; Flags: ignoreversion
#endif
