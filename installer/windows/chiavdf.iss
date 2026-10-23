; chiavdf Windows installer.
;
; Inno Setup 6.  The installer is machine-scoped because the FTDI CDM driver
; is a kernel driver.  It installs all six chiavdf executables next to their
; DLLs, appends the bin directory to the system PATH, and stages the
; unmodified FTDI ftdibus.inf and ftdiport.inf packages with pnputil.  The
; driver is shared with other FTDI devices and is intentionally left in the
; driver store on uninstall.
;
; package-windows-bundle.ps1 passes the paths and version through /D defines.
; The source tree is not embedded in the installer, so only the signed bundle
; and the staged driver directory are needed at compile time.

#ifndef MyAppVersion
#define MyAppVersion "0.0.0"
#endif
#ifndef MyVersionInfoVersion
#define MyVersionInfoVersion "0.0.0.0"
#endif
#ifndef MyBundleDir
#error MyBundleDir must be defined (the signed chiavdf bundle directory)
#endif
#ifndef MyDriverDir
#error MyDriverDir must be defined (the staged FTDI CDM driver directory)
#endif
#ifndef MyOutputDir
#define MyOutputDir "..\..\dist\windows"
#endif
#ifndef MyOutputBaseFilename
#define MyOutputBaseFilename "chiavdf-setup"
#endif
#ifndef MyRepoRoot
#define MyRepoRoot "..\.."
#endif
#if VER < EncodeVer(6, 0, 0)
#error Inno Setup 6 or newer is required
#endif

[Setup]
AppId={{48EFE738-4652-4CA6-B908-3D5317CE7C84}
AppName=chiavdf
AppVersion={#MyAppVersion}
AppVerName=chiavdf {#MyAppVersion}
AppPublisher=Chia Network, Inc.
AppPublisherURL=https://github.com/Chia-Network/chiavdf
AppSupportURL=https://github.com/Chia-Network/chiavdf/issues
AppUpdatesURL=https://github.com/Chia-Network/chiavdf/releases
DefaultDirName={autopf}\Chia\chiavdf
DefaultGroupName=Chia\chiavdf
DisableProgramGroupPage=yes
DisableDirPage=no
UsePreviousAppDir=yes
OutputDir={#MyOutputDir}
OutputBaseFilename={#MyOutputBaseFilename}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=
; x64 is the accepted alias for x64os; kernel drivers cannot use ARM64/x64
; emulation, so do not broaden this to x64compatible.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.19041
ChangesEnvironment=yes
RestartIfNeededByRun=no
UninstallDisplayName=chiavdf {#MyAppVersion}
UninstallDisplayIcon={app}\bin\vdf_client.exe
LicenseFile={#MyRepoRoot}\LICENSE
VersionInfoVersion={#MyVersionInfoVersion}
VersionInfoProductVersion={#MyVersionInfoVersion}
VersionInfoCompany=Chia Network, Inc.
VersionInfoDescription=chiavdf installer
VersionInfoProductName=chiavdf

[InstallDelete]
; Remove stale binaries from an older release before laying down the new set.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\driver"

[Files]
Source: "{#MyBundleDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyDriverDir}\*"; DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyRepoRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

var
  DriverRebootRequired: Boolean;

function NormalizePath(const Value: string): string;
begin
  Result := RemoveBackslashUnlessRoot(Value);
end;

function PathEntryEquals(const A, B: string): Boolean;
begin
  Result := CompareText(NormalizePath(A), NormalizePath(B)) = 0;
end;

procedure AddToSystemPath(const Dir: string);
var
  Paths, NormalizedDir: string;
begin
  NormalizedDir := NormalizePath(Dir);
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    Paths := '';
  if Pos(';' + Uppercase(NormalizedDir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
  begin
    Log('PATH already contains ' + NormalizedDir);
    exit;
  end;
  if Paths = '' then
    Paths := NormalizedDir
  else
    Paths := Paths + ';' + NormalizedDir;
  if not RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    RaiseException('Could not add ' + NormalizedDir + ' to the system PATH.');
  Log('Added to system PATH: ' + NormalizedDir);
end;

procedure RemoveFromSystemPath(const Dir: string);
var
  Paths, NewPaths, Part: string;
  P, Start: Integer;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    exit;
  NewPaths := '';
  Start := 1;
  for P := 1 to Length(Paths) do
  begin
    if Paths[P] = ';' then
    begin
      Part := Copy(Paths, Start, P - Start);
      if (Part <> '') and not PathEntryEquals(Part, Dir) then
      begin
        if NewPaths <> '' then
          NewPaths := NewPaths + ';';
        NewPaths := NewPaths + Part;
      end;
      Start := P + 1;
    end;
  end;
  Part := Copy(Paths, Start, Length(Paths) - Start + 1);
  if (Part <> '') and not PathEntryEquals(Part, Dir) then
  begin
    if NewPaths <> '' then
      NewPaths := NewPaths + ';';
    NewPaths := NewPaths + Part;
  end;
  if NewPaths <> Paths then
  begin
    if RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', NewPaths) then
      Log('Removed from system PATH: ' + NormalizePath(Dir))
    else
      Log('Could not remove from system PATH: ' + NormalizePath(Dir));
  end;
end;

function DriverInstallEnabled: Boolean;
begin
  Result := CompareText(ExpandConstant('{param:DRIVER|1}'), '0') <> 0;
end;

procedure RunPnPUtil(const Arguments, Description: string);
var
  ResultCode: Integer;
begin
  if not Exec(ExpandConstant('{sys}\pnputil.exe'), Arguments, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) then
    RaiseException('Could not run pnputil.exe for ' + Description + '.');
  Log(Format('pnputil.exe %s exited with code %d', [Description, ResultCode]));
  if ResultCode = 3010 then
    DriverRebootRequired := True
  else if ResultCode = 1641 then
    DriverRebootRequired := True
  else if ResultCode = 259 then
    // The package was staged, but no device matched or a better driver is
    // already active. This is normal for a software-only timelord install.
    Log('No matching device or a newer driver is already active for ' + Description + '.')
  else if ResultCode <> 0 then
    RaiseException(Format('pnputil.exe failed for %s with exit code %d.',
      [Description, ResultCode]));
end;

procedure InstallDriverPackage(const InfName: string);
var
  InfPath: string;
begin
  InfPath := ExpandConstant('{app}\driver\') + InfName;
  if not FileExists(InfPath) then
    RaiseException('The FTDI driver package is missing: ' + InfPath);
  RunPnPUtil('/add-driver ' + AddQuotes(InfPath) + ' /install', InfName);
end;

procedure RescanDevices;
var
  ResultCode: Integer;
begin
  if not Exec(ExpandConstant('{sys}\pnputil.exe'), '/scan-devices', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) then
  begin
    Log('Could not run pnputil.exe /scan-devices.');
    exit;
  end;
  Log(Format('pnputil.exe /scan-devices exited with code %d', [ResultCode]));
  if ResultCode = 3010 then
    DriverRebootRequired := True
  else if ResultCode <> 0 then
    Log(Format('pnputil.exe /scan-devices failed with code %d', [ResultCode]));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if DriverInstallEnabled then
    begin
      InstallDriverPackage('ftdibus.inf');
      InstallDriverPackage('ftdiport.inf');
      RescanDevices;
    end
    else
      Log('FTDI driver installation skipped because /DRIVER=0 was supplied.');
    AddToSystemPath(ExpandConstant('{app}\bin'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveFromSystemPath(ExpandConstant('{app}\bin'));
end;

function NeedRestart(): Boolean;
begin
  // Never reboot a silent/winget install. Return 3010 instead and let the
  // package manager decide when to restart.
  Result := DriverRebootRequired and not WizardSilent;
end;

function GetCustomSetupExitCode: Integer;
begin
  if DriverRebootRequired then
    Result := 3010
  else
    Result := 0;
end;
