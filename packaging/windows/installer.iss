#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef AppVersion
  #define AppVersion "0.0.4"
#endif
[Setup]
AppId={{2CF28632-0B53-4C47-A897-FCC64D1E6048}
AppName=Duomi Suno AI Bridge
AppVersion={#AppVersion}
AppPublisher=MrDream
DefaultDirName={autopf}\Duomi Suno AI Bridge
DefaultGroupName=Duomi Suno AI Bridge
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Duomi-Suno-AI-Bridge-{#AppVersion}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#StageDir}\duomi-logo.ico
WizardSmallImageFile={#StageDir}\duomi-logo.png
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\duomi-logo.ico
InfoBeforeFile={#StageDir}\INSTALL.txt
SetupLogging=yes

[Types]
Name: "full"; Description: "VST3 plugin and standalone application"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin for Ableton Live / other DAWs"; Types: full
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#StageDir}\duomi-logo.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\Standalone\Duomi Suno AI Bridge.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#StageDir}\VST3\Duomi Suno AI Bridge.vst3\*"; DestDir: "{code:GetVst3Dir}\Duomi Suno AI Bridge.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\INSTALL.en.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY_NOTICES.zh-CN.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\Configure-FFmpeg.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\Configure-FFmpeg.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Duomi Suno AI Bridge"; Filename: "{app}\Duomi Suno AI Bridge.exe"; Components: standalone
Name: "{group}\Installation guide (English)"; Filename: "{app}\INSTALL.en.txt"
Name: "{group}\Installation guide (Chinese)"; Filename: "{app}\INSTALL.txt"
Name: "{group}\Configure FFmpeg for stem audio"; Filename: "{app}\Configure-FFmpeg.cmd"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"

[Code]
var
  Vst3Page: TInputDirWizardPage;

procedure InitializeWizard;
begin
  Vst3Page := CreateInputDirPage(wpSelectComponents,
    'VST3 plugin folder', 'Close Ableton Live and other DAWs before installing.',
    'Use the default system VST3 folder, or select the VST3 folder scanned by your DAW.', False, '');
  Vst3Page.Add('VST3 folder:');
  if IsAdminInstallMode then
    Vst3Page.Values[0] := ExpandConstant('{commoncf64}\VST3')
  else
    Vst3Page.Values[0] := ExpandConstant('{localappdata}\Programs\Common\VST3');
  Vst3Page.Values[0] := GetPreviousData('VST3Dir', Vst3Page.Values[0]);
  if ExpandConstant('{param:VST3DIR|}') <> '' then
    Vst3Page.Values[0] := ExpandConstant('{param:VST3DIR|}');
end;

function GetVst3Dir(Param: String): String;
begin
  Result := Vst3Page.Values[0];
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = Vst3Page.ID) and not WizardIsComponentSelected('vst3');
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'VST3Dir', Vst3Page.Values[0]);
end;

