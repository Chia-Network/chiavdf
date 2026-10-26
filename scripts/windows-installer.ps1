# Inno Setup installer actions for the Windows release bundle.
#
# package-windows-bundle.ps1 dot-sources this file next to windows-pe.ps1. The
# functions use the repository root, bundle paths, and shared verification
# helpers defined by the calling script.

function Get-InstallerVersionInfoVersion {
    param([Parameter(Mandatory = $true)][string]$Version)
    $match = [regex]::Match($Version, '^[vV]?(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:[.-](\d+))?')
    if (-not $match.Success) {
        return '0.0.0.0'
    }
    $parts = @()
    for ($i = 1; $i -le 4; $i++) {
        if ($match.Groups[$i].Success) {
            # GitHub run IDs are now larger than Int32. Parse as Int64 first,
            # then cap to the Inno Setup per-component limit. CI versions such
            # as 0.0.1-<run_id> therefore collapse to 0.0.1.65535.
            $value = [long]$match.Groups[$i].Value
            if ($value -gt 65535) {
                $value = 65535
            }
            $parts += [int]$value
        } else {
            $parts += 0
        }
    }
    return ($parts -join '.')
}

function Get-IsccPath {
    if (-not [string]::IsNullOrWhiteSpace($env:INNO_SETUP_ISCC)) {
        $configured = $env:INNO_SETUP_ISCC.Trim()
        if (Test-Path -LiteralPath $configured -PathType Leaf) {
            return (Resolve-Path -LiteralPath $configured).Path
        }
        throw "INNO_SETUP_ISCC does not point to ISCC.exe: $configured"
    }
    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ([string]::IsNullOrWhiteSpace($base)) {
            continue
        }
        foreach ($match in @(Get-ChildItem -Path (Join-Path $base 'Inno Setup *\ISCC.exe') -ErrorAction SilentlyContinue)) {
            if (Test-Path -LiteralPath $match.FullName -PathType Leaf) {
                return $match.FullName
            }
        }
    }
    throw 'Inno Setup 6 ISCC.exe was not found. Install it with the pinned install-inno-setup action or set INNO_SETUP_ISCC.'
}

function Invoke-BuildInstaller {
    $version = Require-Text 'INSTALLER_VERSION'
    $paths = Get-BundlePaths $version
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames (Get-RuntimeDllNames)
    Assert-ImportClosure -BinDir $paths.BinDir
    $requireSignatures = Require-Flag 'REQUIRE_SIGNATURES'
    if ($requireSignatures) {
        Assert-Authenticode -BinDir $paths.BinDir
    }
    Assert-FtdiDriverSource -DriverDir $paths.DriverDir
    if (-not (Test-Path -LiteralPath $paths.InstallerScript -PathType Leaf)) {
        throw "Installer script is missing: $($paths.InstallerScript)"
    }
    $iscc = Get-IsccPath
    New-Item -ItemType Directory -Force -Path $paths.DistDir | Out-Null
    foreach ($stale in @(
            $paths.SetupPath,
            "$($paths.SetupPath).sha256",
            $paths.WingetSetupPath,
            "$($paths.WingetSetupPath).sha256"
        )) {
        Remove-Item -LiteralPath $stale -Force -ErrorAction SilentlyContinue
    }
    $arguments = @(
        "/DMyAppVersion=$version",
        "/DMyVersionInfoVersion=$(Get-InstallerVersionInfoVersion $version)",
        "/DMyBundleDir=$($paths.BundleRoot)",
        "/DMyDriverDir=$($paths.DriverDir)",
        "/DMyOutputDir=$($paths.DistDir)",
        "/DMyOutputBaseFilename=$($paths.SetupBaseName)",
        "/DMyRepoRoot=$RepoRoot"
    )
    if ($requireSignatures) {
        $signTool = Require-Text 'INSTALLER_SIGNTOOL'
        $arguments += '/DMySignTool=ChiaSignTool'
        $arguments += "/SChiaSignTool=$signTool"
    }
    $arguments += $paths.InstallerScript
    Write-Host "Compiling installer with $iscc"
    & $iscc @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compiler failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer was not created: $($paths.SetupPath)"
    }
    Write-Host "Built $($paths.SetupPath)"
}

function Invoke-FinalizeInstaller {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    if (Require-Flag 'REQUIRE_SIGNATURES') {
        Assert-Authenticode -Path $paths.SetupPath
    }
    $hash = (Get-FileHash -LiteralPath $paths.SetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$($paths.SetupPath).sha256" -Value $hash -NoNewline
    Publish-ReleaseAlias -SourcePath $paths.SetupPath -AliasPath $paths.WingetSetupPath `
        -IsRelease (Require-Flag 'IS_RELEASE') -Hash $hash `
        -MismatchMessage 'chiavdf-win64-setup.exe does not match the versioned installer.'
    Write-Host "Finalized signed $($paths.SetupPath)"
}

function Invoke-VerifyInstaller {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    $hash = (Get-FileHash -LiteralPath $paths.SetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sidecarPath = "$($paths.SetupPath).sha256"
    if (-not (Test-Path -LiteralPath $sidecarPath -PathType Leaf)) {
        throw "Installer checksum is missing: $sidecarPath"
    }
    $recorded = (Get-Content -LiteralPath $sidecarPath -Raw).Trim().ToLowerInvariant()
    if ($recorded -ne $hash) {
        throw "Installer checksum does not match: $sidecarPath"
    }
    if (Require-Flag 'REQUIRE_SIGNATURES') {
        Assert-Authenticode -Path $paths.SetupPath
    }
    if (Require-Flag 'IS_RELEASE') {
        if (-not (Test-Path -LiteralPath $paths.WingetSetupPath -PathType Leaf)) {
            throw "Winget installer alias is missing: $($paths.WingetSetupPath)"
        }
        $aliasHash = (Get-FileHash -LiteralPath $paths.WingetSetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($aliasHash -ne $hash) {
            throw 'chiavdf-win64-setup.exe does not match the versioned installer.'
        }
        $aliasSidecar = "$($paths.WingetSetupPath).sha256"
        if (-not (Test-Path -LiteralPath $aliasSidecar -PathType Leaf)) {
            throw "Winget installer checksum is missing: $aliasSidecar"
        }
        $recordedAlias = (Get-Content -LiteralPath $aliasSidecar -Raw).Trim().ToLowerInvariant()
        if ($recordedAlias -ne $hash) {
            throw "Winget installer checksum does not match: $aliasSidecar"
        }
    }
    Write-Host "Verified $($paths.SetupPath)"
}

function Get-MachinePathEntryCount {
    param([Parameter(Mandatory = $true)][string]$Entry)
    $environmentKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
    $machinePath = (Get-ItemProperty -LiteralPath $environmentKey -Name Path).Path
    $normalized = $Entry.TrimEnd('\')
    return @($machinePath -split ';' | Where-Object { $_.TrimEnd('\') -ieq $normalized }).Count
}

function Set-MachinePathValue {
    param([Parameter(Mandatory = $true)][string]$Value)
    $environmentKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
    Set-ItemProperty -LiteralPath $environmentKey -Name Path -Value $Value -Type ExpandString
}

function Invoke-TestInstaller {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        if ($env:CI) {
            throw 'The installer smoke test must run elevated in CI.'
        }
        Write-Warning 'Skipping installer smoke test because this process is not elevated.'
        return
    }
    $tempRoot = New-ProbeDirectory
    $installDir = Join-Path $env:ProgramFiles ('Chia\chiavdf-smoke-' + [guid]::NewGuid().ToString('n'))
    try {
        $installLog = Join-Path $tempRoot 'install.log'
        $arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DRIVER=1 /DIR=""$installDir"" /LOG=""$installLog"""
        Write-Host "Running $($paths.SetupPath) $arguments"
        $process = Start-Process -FilePath $paths.SetupPath -ArgumentList $arguments -Wait -PassThru
        if ($process.ExitCode -notin @(0, 3010)) {
            throw "Installer failed with exit code $($process.ExitCode). See $installLog"
        }
        $binDir = Join-Path $installDir 'bin'
        foreach ($exe in $BundleExes) {
            $path = Join-Path $binDir $exe
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Installer did not install $path"
            }
        }
        foreach ($driverFile in @('driver\ftdibus.inf', 'driver\ftdiport.inf', 'driver\amd64\ftdibus.sys')) {
            $path = Join-Path $installDir $driverFile
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Installer did not install $path"
            }
        }
        $enumOutput = (& "$env:SystemRoot\System32\pnputil.exe" /enum-drivers 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) {
            throw "pnputil /enum-drivers failed with exit code $LASTEXITCODE"
        }
        if ($enumOutput -notmatch '(?im)^\s*Original Name:\s+(ftdibus|ftdiport)\.inf\s*$') {
            throw 'pnputil /enum-drivers does not list the staged FTDI driver.'
        }
        if ((Get-MachinePathEntryCount -Entry $binDir) -ne 1) {
            throw "Installer did not add $binDir to the machine PATH exactly once."
        }

        $environmentKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
        $seededPath = (Get-ItemProperty -LiteralPath $environmentKey -Name Path).Path
        $normalizedBin = $binDir.TrimEnd('\')
        $seededPath = (@($seededPath -split ';' | ForEach-Object {
                    if ($_.TrimEnd('\') -ieq $normalizedBin) { $normalizedBin + '\' } else { $_ }
                }) -join ';')
        Set-MachinePathValue -Value $seededPath
        $arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DRIVER=0 /DIR=""$installDir"" /LOG=""$installLog"""
        Write-Host "Reinstalling $($paths.SetupPath) $arguments"
        $process = Start-Process -FilePath $paths.SetupPath -ArgumentList $arguments -Wait -PassThru
        if ($process.ExitCode -notin @(0, 3010)) {
            throw "Installer reinstall failed with exit code $($process.ExitCode). See $installLog"
        }
        if ((Get-MachinePathEntryCount -Entry $binDir) -ne 1) {
            throw "Reinstall left $binDir in the machine PATH more than once."
        }

        Enable-FailFastLoaderErrors
        $forbidden = @($RepoRoot, $installDir, $binDir, $tempRoot)
        foreach ($exe in $BundleExes) {
            Invoke-VersionProbe -ExePath (Join-Path $binDir $exe) -WorkDir $tempRoot -ForbiddenRoots $forbidden
        }
        $uninstaller = Join-Path $installDir 'unins000.exe'
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            throw "Installer did not create $uninstaller"
        }
        $uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait -PassThru
        if ($uninstallProcess.ExitCode -notin @(0, 3010)) {
            throw "Uninstaller failed with exit code $($uninstallProcess.ExitCode)"
        }
        if (Test-Path -LiteralPath $binDir) {
            throw "Uninstaller left $binDir behind."
        }
        if ((Get-MachinePathEntryCount -Entry $binDir) -ne 0) {
            throw "Uninstaller did not remove $normalizedBin from the machine PATH."
        }
    } finally {
        $uninstaller = Join-Path $installDir 'unins000.exe'
        if (Test-Path -LiteralPath $uninstaller) {
            Write-Warning 'Cleaning up a failed installer smoke test.'
            Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait | Out-Null
        }
        if (Test-Path -LiteralPath $installDir) {
            Remove-Item -LiteralPath $installDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
