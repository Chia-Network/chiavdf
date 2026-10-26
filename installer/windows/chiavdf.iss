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
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
OutputDir={#MyOutputDir}
OutputBaseFilename={#MyOutputBaseFilename}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
; x64 is the accepted alias for x64os; kernel drivers cannot use ARM64/x64
; emulation, so do not broaden this to x64compatible.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.19041
ChangesEnvironment=yes
UninstallDisplayName=chiavdf
UninstallDisplayIcon={app}\bin\vdf_client.exe
LicenseFile={#MyRepoRoot}\LICENSE
VersionInfoVersion={#MyVersionInfoVersion}
VersionInfoProductVersion={#MyVersionInfoVersion}
VersionInfoCompany=Chia Network, Inc.
VersionInfoDescription=chiavdf installer
VersionInfoProductName=chiavdf
; The release workflow supplies a SignTool through ISCC /S and defines
; MySignTool. SignedUninstaller signs unins000.exe during compilation.
#ifdef MySignTool
SignTool={#MySignTool}
SignedUninstaller=yes
#endif

[InstallDelete]
; Remove stale binaries only when upgrading the same directory. A fresh install
; or an install to a different directory must not delete files the user put in
; a directory they chose.
Type: filesandordirs; Name: "{app}\bin"; Check: IsSameDirectoryUpgrade
Type: filesandordirs; Name: "{app}\driver"; Check: IsSameDirectoryUpgrade

[Files]
Source: "{#MyBundleDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyDriverDir}\*"; DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyRepoRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{48EFE738-4652-4CA6-B908-3D5317CE7C84}_is1';
  DriverInstallFailedExitCode = 1603;

var
  DriverRebootRequired: Boolean;
  DriverInstallFailed: Boolean;

function PathEntryEquals(const A, B: string): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(A), RemoveBackslashUnlessRoot(B)) = 0;
end;

function IsSameDirectoryUpgrade: Boolean;
var
  PreviousDir: string;
begin
  Result := False;
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, UninstallKey, 'Inno Setup: App Path', PreviousDir) then
    exit;
  Result := PathEntryEquals(PreviousDir, ExpandConstant('{app}'));
end;

function IsAllowedInstallDir(const Dir: string): Boolean;
var
  Candidate, Allowed: string;
begin
  Candidate := RemoveBackslashUnlessRoot(ExpandFileName(Dir));
  Allowed := RemoveBackslashUnlessRoot(ExpandConstant('{autopf}'));
  Result := (CompareText(Candidate, Allowed) = 0) or
    (Pos(UpperCase(Allowed) + '\', UpperCase(Candidate)) = 1);
end;

function InitializeSetup(): Boolean;
var
  RequestedDir: string;
begin
  Result := True;
  RequestedDir := ExpandConstant('{param:DIR|}');
  if (RequestedDir <> '') and not IsAllowedInstallDir(RequestedDir) then
  begin
    MsgBox('chiavdf must be installed under ' + ExpandConstant('{autopf}') + '.',
      mbError, MB_OK);
    Result := False;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    if not IsAllowedInstallDir(WizardDirValue) then
    begin
      MsgBox('chiavdf must be installed under ' + ExpandConstant('{autopf}') + '.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function UpdateSystemPath(const Paths, Dir: string; Add: Boolean; var NewPaths: string): Boolean;
var
  Part, NormalizedDir: string;
  P, Start: Integer;
  Found: Boolean;
begin
  NewPaths := '';
  Found := False;
  NormalizedDir := RemoveBackslashUnlessRoot(Dir);
  Start := 1;
  for P := 1 to Length(Paths) + 1 do
  begin
    if (P > Length(Paths)) or (Paths[P] = ';') then
    begin
      Part := Copy(Paths, Start, P - Start);
      if Part <> '' then
      begin
        if PathEntryEquals(Part, Dir) then
        begin
          Found := True;
          if Add then
          begin
            if NewPaths <> '' then
              NewPaths := NewPaths + ';';
            NewPaths := NewPaths + Part;
          end;
        end
        else
        begin
          if NewPaths <> '' then
            NewPaths := NewPaths + ';';
          NewPaths := NewPaths + Part;
        end;
      end;
      Start := P + 1;
    end;
  end;
  if Add and not Found then
  begin
    if NewPaths <> '' then
      NewPaths := NewPaths + ';';
    NewPaths := NewPaths + NormalizedDir;
  end;
  Result := NewPaths <> Paths;
end;

procedure AddToSystemPath(const Dir: string);
var
  Paths, NewPaths: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    RaiseException('Could not read the system PATH from the registry.');
  if not UpdateSystemPath(Paths, Dir, True, NewPaths) then
  begin
    Log('PATH already contains ' + RemoveBackslashUnlessRoot(Dir));
    exit;
  end;
  if not RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', NewPaths) then
    RaiseException('Could not add ' + RemoveBackslashUnlessRoot(Dir) + ' to the system PATH.');
  Log('Added to system PATH: ' + RemoveBackslashUnlessRoot(Dir));
end;

procedure RemoveFromSystemPath(const Dir: string);
var
  Paths, NewPaths: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
  begin
    Log('Could not read the system PATH from the registry; leaving it unchanged.');
    exit;
  end;
  if UpdateSystemPath(Paths, Dir, False, NewPaths) then
  begin
    if RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', NewPaths) then
      Log('Removed from system PATH: ' + RemoveBackslashUnlessRoot(Dir))
    else
      Log('Could not remove from system PATH: ' + RemoveBackslashUnlessRoot(Dir));
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
  begin
    DriverInstallFailed := True;
    Log('Could not run pnputil.exe for ' + Description + '.');
    exit;
  end;
  Log(Format('pnputil.exe %s exited with code %d', [Description, ResultCode]));
  if (ResultCode = 3010) or (ResultCode = 1641) then
    DriverRebootRequired := True
  else if ResultCode = 259 then
    // The package was staged, but no device matched or a better driver is
    // already active. This is normal for a software-only timelord install.
    Log('No matching device or a newer driver is already active for ' + Description + '.')
  else if ResultCode <> 0 then
  begin
    DriverInstallFailed := True;
    Log(Format('pnputil.exe failed for %s with exit code %d.', [Description, ResultCode]));
  end;
end;

procedure InstallDriverPackage(const InfName: string);
var
  InfPath: string;
begin
  InfPath := ExpandConstant('{app}\driver\') + InfName;
  if not FileExists(InfPath) then
  begin
    DriverInstallFailed := True;
    Log('The FTDI driver package is missing: ' + InfPath);
    exit;
  end;
  RunPnPUtil('/add-driver ' + AddQuotes(InfPath) + ' /install', InfName);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Add PATH before the driver so a driver failure cannot leave a usable
    // install without a PATH entry.
    AddToSystemPath(ExpandConstant('{app}\bin'));
    if DriverInstallEnabled then
    begin
      InstallDriverPackage('ftdibus.inf');
      InstallDriverPackage('ftdiport.inf');
    end
    else
      Log('FTDI driver installation skipped because /DRIVER=0 was supplied.');
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
  if DriverInstallFailed then
    Result := DriverInstallFailedExitCode
  else if DriverRebootRequired then
    Result := 3010
  else
    Result := 0;
end;
