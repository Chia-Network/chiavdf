# Build the Windows release assets so they start on a machine that has never
# seen this checkout.
#
# Windows loads a DLL from the directory containing the executable, then
# System32, the Windows directory, the current directory, and PATH. It does
# not look in a sibling lib directory. Every shipped exe and DLL therefore
# lives in bin/. The set of DLLs is the import closure of those binaries:
# each non-system import must exist in bin/ under the name in the import
# table. MPIR's proxy loads its CPU variant from the directory that contains
# mpir.dll, so the other native DLLs in that directory ship with it. The VC++
# runtime is copied from the MSVC redist and is part of the closure. The zip
# is launched from an empty directory with a PATH that contains only the
# system directories. ListSignFiles reports the six chiavdf executables and the
# MPIR DLLs for Chia-Network/actions/sign/azure-windows; the VC++ runtime and
# the FTDI DLLs keep their vendor signatures. Package verifies every shipped
# Authenticode signature, requires a timestamp and the expected Chia signer for
# our signed files, and accepts the vendor chain for the remaining DLLs.
#
# BuildInstaller turns the signed bundle and the staged FTDI CDM driver into a
# machine-scope Inno Setup installer. The installer puts all six tools on the
# system PATH and stages ftdibus.inf/ftdiport.inf with pnputil, so one winget
# install covers both software timelords and ASIC hosts. The portable zip
# remains available for users who cannot elevate. ListInstallerSignFile,
# FinalizeInstaller, VerifyInstaller, and TestInstaller cover the installer's
# signature, signed hash/stable alias, and an install/uninstall smoke test with
# driver staging disabled.
#
# EXPECTED_SIGNER_SUBJECT (regex) and EXPECTED_SIGNER_THUMBPRINT optionally pin
# the publisher for our signed files. When both are unset, the subject must
# contain "Chia Network" (the Artifact Signing certificate profile).
# EXPECTED_VENDOR_SUBJECT optionally pins the vendor DLLs; the default accepts
# Microsoft and FTDI signatures.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Assemble', 'ListSignFiles', 'Package', 'BuildInstaller', 'ListInstallerSignFile', 'FinalizeInstaller', 'VerifyInstaller', 'TestInstaller')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows-pe.ps1')

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$InstallerScript = Join-Path $RepoRoot 'installer/windows/chiavdf.iss'

$BundleExes = @(
    'vdf_client.exe',
    'vdf_bench.exe',
    'hw_vdf_client.exe',
    'emu_hw_vdf_client.exe',
    'hw_test.exe',
    'emu_hw_test.exe'
)

$DllSourceDirs = @(
    (Join-Path $RepoRoot 'mpir_gc_x64'),
    (Join-Path $RepoRoot 'src/hw/libft4222')
)

# Windows DLLs the loader finds in System32. The VC++ 2015-2022 runtime is
# intentionally absent: a zip that does not carry it fails the closure.
$SystemDllNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($name in @(
        'advapi32.dll', 'bcrypt.dll', 'bcryptprimitives.dll', 'cabinet.dll', 'cfgmgr32.dll',
        'combase.dll', 'crypt32.dll', 'cryptbase.dll', 'dbghelp.dll', 'dnsapi.dll',
        'gdi32.dll', 'imagehlp.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll',
        'kernelbase.dll', 'msvcrt.dll', 'mswsock.dll', 'normaliz.dll', 'nsi.dll',
        'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'powrprof.dll', 'profapi.dll',
        'psapi.dll', 'rpcrt4.dll', 'sechost.dll', 'secur32.dll', 'setupapi.dll',
        'shell32.dll', 'shlwapi.dll', 'sspicli.dll', 'ucrtbase.dll', 'user32.dll',
        'userenv.dll', 'version.dll', 'winmm.dll', 'wintrust.dll', 'wldap32.dll',
        'ws2_32.dll', 'wsock32.dll'
    )) {
    [void]$SystemDllNames.Add($name)
}

function Require-Text {
    param([Parameter(Mandatory = $true)][string]$Name)
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "$Name is required."
    }
    return $value.Trim()
}

function Require-Flag {
    param([Parameter(Mandatory = $true)][string]$Name)
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($value -ne 'true' -and $value -ne 'false') {
        throw "$Name must be 'true' or 'false' (got '$value')."
    }
    return $value -eq 'true'
}

function Get-BundlePaths {
    param([Parameter(Mandatory = $true)][string]$Version)
    if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9._+-]*$') {
        throw "INSTALLER_VERSION is not safe to use as a directory name: $Version"
    }
    $baseName = "chiavdf-$Version-windows-amd64"
    $distDir = Join-Path $RepoRoot 'dist/windows'
    $bundleRoot = Join-Path $distDir $baseName
    $setupName = "$baseName-setup"
    [pscustomobject]@{
        BaseName         = $baseName
        DistDir          = $distDir
        BundleRoot       = $bundleRoot
        BinDir           = Join-Path $bundleRoot 'bin'
        AssetPath        = Join-Path $distDir "$baseName.zip"
        WingetAssetPath  = Join-Path $distDir 'chiavdf-win64.zip'
        DriverDir        = Join-Path $RepoRoot 'src/hw/libft4222/driver'
        InstallerScript  = $InstallerScript
        SetupBaseName    = $setupName
        SetupPath        = Join-Path $distDir "$setupName.exe"
        WingetSetupPath  = Join-Path $distDir 'chiavdf-win64-setup.exe'
    }
}

function Test-VcRuntimeName {
    param([Parameter(Mandatory = $true)][string]$Leaf)
    return $Leaf -match '^(?i)(?:vcruntime140|msvcp140|concrt140|vccorlib140)(?:_[0-9a-z]+)*\.dll$'
}

function Test-SystemDllName {
    param([Parameter(Mandatory = $true)][string]$Leaf)
    if (Test-VcRuntimeName $Leaf) {
        return $false
    }
    if ($Leaf -like 'api-ms-win-*' -or $Leaf -like 'ext-ms-*') {
        return $true
    }
    return $SystemDllNames.Contains($Leaf)
}

function Get-DllCandidates {
    $map = [System.Collections.Generic.Dictionary[string, System.IO.FileInfo]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    foreach ($dir in $DllSourceDirs) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
            throw "DLL source directory is missing: $dir"
        }
        $dlls = @(Get-ChildItem -LiteralPath $dir -Filter '*.dll' -File)
        if ($dlls.Count -eq 0) {
            throw "DLL source directory contains no DLLs: $dir"
        }
        foreach ($dll in $dlls) {
            Assert-NativeAmd64Dll $dll.FullName | Out-Null
            if ($map.ContainsKey($dll.Name)) {
                throw "Duplicate runtime DLL name: $($dll.Name)"
            }
            $map.Add($dll.Name, $dll)
        }
    }
    return $map
}

function Get-MsvcCrtDirectory {
    $roots = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
        $roots.Add($env:VCToolsRedistDir.Trim())
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    } else {
        $vswhere = $null
    }
    if ($vswhere -and (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        $installs = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
        foreach ($install in $installs) {
            if (-not [string]::IsNullOrWhiteSpace($install)) {
                $roots.Add((Join-Path $install.Trim() 'VC\Redist\MSVC'))
            }
        }
    }
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        $match = @(Get-ChildItem -LiteralPath $root -Recurse -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.FullName -match '[\\/]x64[\\/]Microsoft\.VC\d+\.CRT$' -and
                    (Test-Path -LiteralPath (Join-Path $_.FullName 'vcruntime140.dll') -PathType Leaf)
                } |
                Sort-Object FullName -Descending |
                Select-Object -First 1)
        if ($match.Count -eq 1) {
            return $match[0].FullName
        }
    }
    throw 'MSVC x64 CRT redist directory was not found. Refusing to copy the VC++ runtime from System32.'
}

function Find-CrtDll {
    param(
        [Parameter(Mandatory = $true)][string]$CrtDirectory,
        [Parameter(Mandatory = $true)][string]$Leaf
    )
    $match = @(Get-ChildItem -LiteralPath $CrtDirectory -File | Where-Object { $_.Name -eq $Leaf })
    if ($match.Count -ne 1) {
        throw "MSVC redist directory $CrtDirectory does not contain $Leaf. Refusing to take it from System32."
    }
    return $match[0].FullName
}

function Get-RequiredRuntimeDlls {
    param([Parameter(Mandatory = $true)][string[]]$ExePaths)
    $candidates = Get-DllCandidates
    $crtDirectory = Get-MsvcCrtDirectory
    $required = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $contributedDirs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($exe in $ExePaths) {
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw "Missing expected binary: $exe"
        }
        $queue.Enqueue((Resolve-Path -LiteralPath $exe).Path)
    }

    $scanSiblings = $true
    while ($queue.Count -gt 0 -or $scanSiblings) {
        if ($queue.Count -eq 0) {
            $scanSiblings = $false
            foreach ($candidate in @($candidates.Values)) {
                $dir = [System.IO.Path]::GetDirectoryName($candidate.FullName)
                if ($contributedDirs.Contains($dir) -and $seen.Add($candidate.Name)) {
                    $required.Add([pscustomobject]@{ Name = $candidate.Name; Source = $candidate.FullName })
                    $queue.Enqueue($candidate.FullName)
                    $scanSiblings = $true
                }
            }
            continue
        }

        $image = Assert-NativeAmd64Dll $queue.Dequeue()
        foreach ($import in @($image.Imports)) {
            if ([string]::IsNullOrWhiteSpace($import)) {
                continue
            }
            $leaf = [System.IO.Path]::GetFileName($import)
            if ([string]::IsNullOrWhiteSpace($leaf)) {
                throw "Empty import name in $($image.Path)"
            }
            if ((Test-SystemDllName $leaf) -or -not $seen.Add($leaf)) {
                continue
            }
            if (Test-VcRuntimeName $leaf) {
                $source = Find-CrtDll $crtDirectory $leaf
                Assert-NativeAmd64Dll $source | Out-Null
            } elseif ($candidates.ContainsKey($leaf)) {
                $source = $candidates[$leaf].FullName
                [void]$contributedDirs.Add([System.IO.Path]::GetDirectoryName($source))
            } else {
                throw "$($image.Path) imports $leaf, which is not in the Windows runtime set."
            }
            $required.Add([pscustomobject]@{ Name = $leaf; Source = $source })
            $queue.Enqueue($source)
        }
    }
    return @($required.ToArray())
}

function Get-BuildExePaths {
    return @($BundleExes | ForEach-Object { Join-Path $RepoRoot (Join-Path 'build' $_) })
}

function Assert-BundleRoot {
    param(
        [Parameter(Mandatory = $true)][string]$BundleRoot,
        [Parameter(Mandatory = $true)][string[]]$DllNames
    )
    if (-not (Test-Path -LiteralPath $BundleRoot -PathType Container)) {
        throw "Bundle directory is missing: $BundleRoot"
    }
    $children = @(Get-ChildItem -LiteralPath $BundleRoot -Force)
    $directories = @($children | Where-Object { $_.PSIsContainer })
    $strayFiles = @($children | Where-Object { -not $_.PSIsContainer })
    if ($strayFiles.Count -ne 0 -or $directories.Count -ne 1 -or $directories[0].Name -ne 'bin') {
        $found = @($children | ForEach-Object { $_.Name } | Sort-Object) -join ', '
        throw "Windows bundle must contain only bin/. Found: $found"
    }
    $binFiles = @(Get-ChildItem -LiteralPath $directories[0].FullName -Force)
    $nested = @($binFiles | Where-Object { $_.PSIsContainer })
    if ($nested.Count -gt 0) {
        throw "bin/ must be a flat directory. Nested entries: $($nested.Name -join ', ')"
    }
    $actual = @($binFiles | ForEach-Object { $_.Name })
    $expected = @($BundleExes + $DllNames)
    $missing = @($expected | Where-Object { $actual -cnotcontains $_ })
    $extra = @($actual | Where-Object { $expected -cnotcontains $_ })
    if ($actual.Count -ne (@($BundleExes).Count + @($DllNames).Count) -or
        $actual.Count -ne $expected.Count -or
        $missing.Count -gt 0 -or
        $extra.Count -gt 0) {
        $details = @()
        if ($missing.Count -gt 0) { $details += "missing $($missing -join ', ')" }
        if ($extra.Count -gt 0) { $details += "extra $($extra -join ', ')" }
        if ($details.Count -eq 0) { $details += "file count $($actual.Count)" }
        throw "bin/ does not match the shipped set ($($details -join '; '))."
    }
}

function Copy-BundlePayload {
    param(
        [Parameter(Mandatory = $true)][string]$BinDir,
        [Parameter(Mandatory = $true)]$Dlls
    )
    foreach ($exe in $BundleExes) {
        $source = Join-Path $RepoRoot (Join-Path 'build' $exe)
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Missing expected binary: $source"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $BinDir $exe) -Force
    }
    foreach ($dll in @($Dlls)) {
        Copy-Item -LiteralPath $dll.Source -Destination (Join-Path $BinDir $dll.Name) -Force
    }
}

function Assert-ImportClosure {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    $files = @(Get-ChildItem -LiteralPath $BinDir -File -Force)
    foreach ($file in $files) {
        $image = Assert-NativeAmd64Dll $file.FullName
        foreach ($import in @($image.Imports)) {
            $leaf = [System.IO.Path]::GetFileName($import)
            if ([string]::IsNullOrWhiteSpace($leaf) -or (Test-SystemDllName $leaf)) {
                continue
            }
            $present = @($files | Where-Object { $_.Name -ceq $leaf })
            if ($present.Count -ne 1) {
                throw "$($file.Name) imports $leaf, which is not in bin/ under that name."
            }
        }
    }
}

function Get-ShippedBinaries {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    $binaries = @(Get-ChildItem -LiteralPath $BinDir -File -Force | Sort-Object Name)
    if ($binaries.Count -eq 0) {
        throw "bin/ has no files: $BinDir"
    }
    $unexpected = @($binaries | Where-Object { $_.Extension -notin '.exe', '.dll' } | ForEach-Object { $_.Name })
    if ($unexpected.Count -gt 0) {
        throw "Refusing to sign or publish a non-binary in bin/: $($unexpected -join ', ')"
    }
    return $binaries
}

function Get-BinariesWeSign {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    $mpirRoot = (Resolve-Path -LiteralPath (Join-Path $RepoRoot 'mpir_gc_x64')).Path
    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($exe in $BundleExes) {
        $path = Join-Path $BinDir $exe
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing expected binary to sign: $path"
        }
        $paths.Add($path)
    }
    foreach ($dll in @(Get-RequiredRuntimeDlls (Get-BuildExePaths))) {
        $sourceDir = [System.IO.Path]::GetDirectoryName($dll.Source)
        if (-not $sourceDir.Equals($mpirRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $path = Join-Path $BinDir $dll.Name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing MPIR DLL to sign: $path"
        }
        $paths.Add($path)
    }
    $names = @($paths | ForEach-Object { [System.IO.Path]::GetFileName($_) })
    if ($names -cnotcontains 'mpir.dll') {
        throw 'mpir.dll was not selected for signing.'
    }
    return @($paths.ToArray())
}

function Assert-Authenticode {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    $expectedSubject = [Environment]::GetEnvironmentVariable('EXPECTED_SIGNER_SUBJECT')
    $expectedThumbprint = [Environment]::GetEnvironmentVariable('EXPECTED_SIGNER_THUMBPRINT')
    $expectedVendorSubject = [Environment]::GetEnvironmentVariable('EXPECTED_VENDOR_SUBJECT')
    if ([string]::IsNullOrWhiteSpace($expectedSubject) -and [string]::IsNullOrWhiteSpace($expectedThumbprint)) {
        $expectedSubject = '(?i)Chia ?Network'
    }
    if ([string]::IsNullOrWhiteSpace($expectedVendorSubject)) {
        $expectedVendorSubject = '(?i)Microsoft|FTDI|Future Technology'
    }
    $signedNames = @(Get-BinariesWeSign -BinDir $BinDir | ForEach-Object { [System.IO.Path]::GetFileName($_) })
    foreach ($binary in @(Get-ShippedBinaries $BinDir)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "$($binary.Name) has no valid Authenticode signature: $($signature.StatusMessage)"
        }
        if ($null -eq $signature.SignerCertificate) {
            throw "$($binary.Name) has no signer certificate."
        }
        if ($signedNames -contains $binary.Name) {
            if ($null -eq $signature.TimeStamperCertificate) {
                throw "$($binary.Name) has no timestamp."
            }
            if (-not [string]::IsNullOrWhiteSpace($expectedThumbprint)) {
                $actual = ($signature.SignerCertificate.Thumbprint -replace '\s', '').ToUpperInvariant()
                $wanted = ($expectedThumbprint -replace '\s', '').ToUpperInvariant()
                if ($actual -ne $wanted) {
                    throw "$($binary.Name) signer thumbprint $actual does not match $wanted."
                }
            }
            if (-not [string]::IsNullOrWhiteSpace($expectedSubject) -and
                $signature.SignerCertificate.Subject -notmatch $expectedSubject) {
                throw "$($binary.Name) signer subject '$($signature.SignerCertificate.Subject)' does not match '$expectedSubject'."
            }
        } elseif ($signature.SignerCertificate.Subject -notmatch $expectedVendorSubject) {
            throw "$($binary.Name) vendor signer '$($signature.SignerCertificate.Subject)' does not match '$expectedVendorSubject'."
        }
    }
}

function Get-SystemOnlyPath {
    $systemRoot = [Environment]::GetEnvironmentVariable('SystemRoot')
    if ([string]::IsNullOrWhiteSpace($systemRoot)) {
        throw 'SystemRoot is not set. Refusing to launch shipped binaries with an implicit PATH.'
    }
    $systemRootFull = [System.IO.Path]::GetFullPath($systemRoot)
    $system32 = [System.IO.Path]::GetFullPath((Join-Path $systemRootFull 'System32'))
    if (-not (Test-Path -LiteralPath $system32 -PathType Container)) {
        throw "System32 is missing: $system32"
    }
    return "$system32;$systemRootFull"
}

function Assert-SystemOnlyPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string[]]$ForbiddenRoots
    )
    $systemOnly = Get-SystemOnlyPath
    if (-not [string]::Equals($PathValue, $systemOnly, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Child PATH is not limited to the system directories. Stored PATH=$PathValue"
    }
    $entries = @($PathValue -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($entries.Count -ne 2) {
        throw "Launch PATH must contain only System32 and the Windows directory. PATH=$PathValue"
    }
    foreach ($entry in $entries) {
        $full = [System.IO.Path]::GetFullPath($entry)
        foreach ($root in $ForbiddenRoots) {
            if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path -LiteralPath $root)) {
                throw "Refusing to launch because a PATH exclusion ($root) is missing."
            }
            $rootFull = [System.IO.Path]::GetFullPath($root).TrimEnd('\')
            $prefix = $rootFull + '\'
            if ($full.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase) -or
                $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Launch PATH includes '$full', which is inside '$rootFull'. A missing DLL would be hidden."
            }
        }
    }
}

function Enable-FailFastLoaderErrors {
    if (-not ('ChiavdfLoaderErrors' -as [type])) {
        Add-Type -Namespace ChiavdfLoaderErrors -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern uint SetErrorMode(uint uMode);
'@
    }
    # Missing DLLs must fail the process. A modal loader dialog would hang CI.
    $semFailCriticalErrors = 0x0001
    $semNoGpFaultErrorBox = 0x0002
    $semNoOpenFileErrorBox = 0x8000
    [void][ChiavdfLoaderErrors.Native]::SetErrorMode(
        $semFailCriticalErrors -bor $semNoGpFaultErrorBox -bor $semNoOpenFileErrorBox
    )
}

function Invoke-VersionProbe {
    param(
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$WorkDir,
        [Parameter(Mandatory = $true)][string[]]$ForbiddenRoots
    )
    $leaf = [System.IO.Path]::GetFileName($ExePath)
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $ExePath
    $psi.Arguments = '--version'
    $psi.WorkingDirectory = $WorkDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables['Path'] = Get-SystemOnlyPath
    $storedPath = $psi.EnvironmentVariables['Path']
    Assert-SystemOnlyPath -PathValue $storedPath -ForbiddenRoots $ForbiddenRoots

    Write-Host "Launching $leaf --version"
    Write-Host "  exe: $ExePath"
    Write-Host "  cwd: $WorkDir"
    Write-Host "  PATH: $storedPath"

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    try {
        if (-not $process.Start()) {
            throw "Failed to start $leaf"
        }
    } catch {
        throw "Failed to start ${leaf}: $($_.Exception.Message)"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        try { $process.Kill() } catch { }
        throw "$leaf did not exit within 120 seconds. A missing DLL may be waiting on a loader dialog."
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    if ($process.ExitCode -ne 0) {
        throw @"
$leaf --version failed with exit code $($process.ExitCode).
stdout:
$stdout
stderr:
$stderr
"@
    }
    if ($stdout -notmatch ('(?m)' + [regex]::Escape($leaf) + ' \S+')) {
        throw @"
$leaf --version exited 0 but did not print a version line.
stdout:
$stdout
stderr:
$stderr
"@
    }
    Write-Host ($stdout.Trim())
}

function New-ProbeDirectory {
    $parent = [Environment]::GetEnvironmentVariable('RUNNER_TEMP')
    if ([string]::IsNullOrWhiteSpace($parent)) {
        $parent = [System.IO.Path]::GetTempPath()
    }
    $dir = Join-Path $parent ('chiavdf-winzip-' + [guid]::NewGuid().ToString('n'))
    New-Item -ItemType Directory -Path $dir | Out-Null
    return (Resolve-Path -LiteralPath $dir).Path
}

function Assert-ZipRoot {
    param(
        [Parameter(Mandatory = $true)][string]$ExtractDir,
        [Parameter(Mandatory = $true)][string]$BaseName
    )
    $top = @(Get-ChildItem -LiteralPath $ExtractDir -Force)
    if ($top.Count -ne 1 -or -not $top[0].PSIsContainer -or $top[0].Name -ne $BaseName) {
        $found = @($top | ForEach-Object { $_.Name }) -join ', '
        throw "Zip root must be exactly $BaseName. Found: $found"
    }
    return $top[0].FullName
}

function Publish-BundleZip {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][bool]$IsRelease
    )
    New-Item -ItemType Directory -Force -Path $Paths.DistDir | Out-Null
    if (Test-Path -LiteralPath $Paths.AssetPath) {
        Remove-Item -LiteralPath $Paths.AssetPath -Force
    }
    Push-Location $Paths.DistDir
    try {
        Compress-Archive -Path $Paths.BaseName -DestinationPath "$($Paths.BaseName).zip" -Force
    } finally {
        Pop-Location
    }
    if (-not (Test-Path -LiteralPath $Paths.AssetPath -PathType Leaf)) {
        throw "Windows zip was not created: $($Paths.AssetPath)"
    }
    $hash = (Get-FileHash -LiteralPath $Paths.AssetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$($Paths.AssetPath).sha256" -Value $hash -NoNewline
    if (-not $IsRelease) {
        Remove-Item -LiteralPath $Paths.WingetAssetPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "$($Paths.WingetAssetPath).sha256" -Force -ErrorAction SilentlyContinue
        return
    }
    Copy-Item -LiteralPath $Paths.AssetPath -Destination $Paths.WingetAssetPath -Force
    Set-Content -LiteralPath "$($Paths.WingetAssetPath).sha256" -Value $hash -NoNewline
    $aliasHash = (Get-FileHash -LiteralPath $Paths.WingetAssetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($aliasHash -ne $hash) {
        throw 'chiavdf-win64.zip does not match the versioned Windows zip.'
    }
}

function Get-InstallerVersionInfoVersion {
    param([Parameter(Mandatory = $true)][string]$Version)
    $match = [regex]::Match($Version, '^[vV]?(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:[.-](\d+))?')
    if (-not $match.Success) {
        return '0.0.0.0'
    }
    $parts = @()
    for ($i = 1; $i -le 4; $i++) {
        if ($match.Groups[$i].Success -and $match.Groups[$i].Value -ne '') {
            # GitHub run IDs are now larger than Int32. Parse as Int64 first,
            # then cap to the Inno Setup per-component limit.
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
    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:INNO_SETUP_ISCC)) {
        $candidates.Add($env:INNO_SETUP_ISCC.Trim())
    }
    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($command) {
        $candidates.Add($command.Source)
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ([string]::IsNullOrWhiteSpace($base)) {
            continue
        }
        foreach ($match in @(Get-ChildItem -Path (Join-Path $base 'Inno Setup *\ISCC.exe') -ErrorAction SilentlyContinue)) {
            $candidates.Add($match.FullName)
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Inno Setup 6 ISCC.exe was not found. Install it (choco install innosetup -y) or set INNO_SETUP_ISCC.'
}

function Assert-DriverSource {
    param([Parameter(Mandatory = $true)][string]$DriverDir)
    if (-not (Test-Path -LiteralPath $DriverDir -PathType Container)) {
        throw "FTDI driver directory is missing: $DriverDir. Run scripts/get-libft4222.ps1 install."
    }
    $required = @(
        'ftdibus.inf',
        'ftdibus.cat',
        'ftdiport.inf',
        'ftdiport.cat',
        'amd64\ftdibus.sys',
        'amd64\ftbusui.dll',
        'amd64\ftd2xx64.dll',
        'amd64\ftlang.dll',
        'amd64\ftser2k.sys',
        'amd64\ftserui2.dll',
        'amd64\ftcserco.dll',
        'i386\ftd2xx.dll',
        'i386\ftdibus.sys',
        'i386\ftbusui.dll',
        'i386\ftlang.dll',
        'i386\ftser2k.sys',
        'i386\ftserui2.dll',
        'i386\ftcserco.dll'
    )
    foreach ($relative in $required) {
        $path = Join-Path $DriverDir $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "FTDI driver file is missing: $path"
        }
    }
    $inf = Get-Content -LiteralPath (Join-Path $DriverDir 'ftdibus.inf') -Raw
    if ($inf -notmatch [regex]::Escape('USB\VID_0403&PID_601C')) {
        throw 'ftdibus.inf does not declare the FT4222H VID/PID (0403:601c).'
    }
    $expectedVendor = [Environment]::GetEnvironmentVariable('EXPECTED_VENDOR_SUBJECT')
    if ([string]::IsNullOrWhiteSpace($expectedVendor)) {
        # WHQL-certified FTDI catalogs are normally signed by Microsoft's
        # Windows Hardware Compatibility Publisher, not by FTDI itself.
        $expectedVendor = '(?i)Microsoft|FTDI|Future Technology'
    }
    foreach ($catalog in @('ftdibus.cat', 'ftdiport.cat')) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $DriverDir $catalog)
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "$catalog has no valid Authenticode signature: $($signature.StatusMessage)"
        }
        if ($null -eq $signature.SignerCertificate) {
            throw "$catalog has no signer certificate."
        }
        if ($signature.SignerCertificate.Subject -notmatch $expectedVendor) {
            throw "$catalog signer '$($signature.SignerCertificate.Subject)' does not match '$expectedVendor'."
        }
    }
}

function Publish-InstallerAlias {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][bool]$IsRelease,
        [Parameter(Mandatory = $true)][string]$Hash
    )
    if (-not $IsRelease) {
        Remove-Item -LiteralPath $Paths.WingetSetupPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "$($Paths.WingetSetupPath).sha256" -Force -ErrorAction SilentlyContinue
        return
    }
    Copy-Item -LiteralPath $Paths.SetupPath -Destination $Paths.WingetSetupPath -Force
    Set-Content -LiteralPath "$($Paths.WingetSetupPath).sha256" -Value $Hash -NoNewline
    $aliasHash = (Get-FileHash -LiteralPath $Paths.WingetSetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($aliasHash -ne $Hash) {
        throw 'chiavdf-win64-setup.exe does not match the versioned installer.'
    }
}

function Invoke-BuildInstaller {
    $version = Require-Text 'INSTALLER_VERSION'
    $paths = Get-BundlePaths $version
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames (Get-RuntimeDllNames)
    Assert-ImportClosure -BinDir $paths.BinDir
    if (Require-Flag 'REQUIRE_SIGNATURES') {
        Assert-Authenticode -BinDir $paths.BinDir
    }
    Assert-DriverSource -DriverDir $paths.DriverDir
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
        "/DMyRepoRoot=$RepoRoot",
        $paths.InstallerScript
    )
    Write-Host "Compiling installer with $iscc"
    & $iscc @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compiler failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer was not created: $($paths.SetupPath)"
    }
    Write-Host "Built unsigned $($paths.SetupPath); sign it before FinalizeInstaller."
}

function Invoke-ListInstallerSignFile {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    return @($paths.SetupPath)
}

function Invoke-FinalizeInstaller {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    if (Require-Flag 'REQUIRE_SIGNATURES') {
        Assert-InstallerSignature -Path $paths.SetupPath
    }
    $hash = (Get-FileHash -LiteralPath $paths.SetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$($paths.SetupPath).sha256" -Value $hash -NoNewline
    Publish-InstallerAlias -Paths $paths -IsRelease (Require-Flag 'IS_RELEASE') -Hash $hash
    Write-Host "Finalized signed $($paths.SetupPath)"
}

function Assert-InstallerSignature {
    param([Parameter(Mandatory = $true)][string]$Path)
    $expectedSubject = [Environment]::GetEnvironmentVariable('EXPECTED_SIGNER_SUBJECT')
    $expectedThumbprint = [Environment]::GetEnvironmentVariable('EXPECTED_SIGNER_THUMBPRINT')
    if ([string]::IsNullOrWhiteSpace($expectedSubject) -and [string]::IsNullOrWhiteSpace($expectedThumbprint)) {
        $expectedSubject = '(?i)Chia ?Network'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "$Path has no valid Authenticode signature: $($signature.StatusMessage)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "$Path has no signer certificate."
    }
    if ($null -eq $signature.TimeStamperCertificate) {
        throw "$Path has no timestamp."
    }
    if (-not [string]::IsNullOrWhiteSpace($expectedThumbprint)) {
        $actual = ($signature.SignerCertificate.Thumbprint -replace '\s', '').ToUpperInvariant()
        $wanted = ($expectedThumbprint -replace '\s', '').ToUpperInvariant()
        if ($actual -ne $wanted) {
            throw "$Path signer thumbprint $actual does not match $wanted."
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($expectedSubject) -and
        $signature.SignerCertificate.Subject -notmatch $expectedSubject) {
        throw "$Path signer subject '$($signature.SignerCertificate.Subject)' does not match '$expectedSubject'."
    }
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
        Assert-InstallerSignature -Path $paths.SetupPath
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

function Invoke-TestInstaller {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (-not (Test-Path -LiteralPath $paths.SetupPath -PathType Leaf)) {
        throw "Installer does not exist: $($paths.SetupPath)"
    }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warning 'Skipping installer smoke test because this process is not elevated.'
        return
    }
    $tempRoot = New-ProbeDirectory
    $installDir = Join-Path $tempRoot 'install'
    $installed = $false
    try {
        $installLog = Join-Path $tempRoot 'install.log'
        $arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DRIVER=0 /DIR=""$installDir"" /LOG=""$installLog"""
        Write-Host "Running $($paths.SetupPath) $arguments"
        $process = Start-Process -FilePath $paths.SetupPath -ArgumentList $arguments -Wait -PassThru
        if ($process.ExitCode -notin @(0, 3010)) {
            throw "Installer failed with exit code $($process.ExitCode). See $installLog"
        }
        $installed = $true
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
        $environmentKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
        $machinePath = (Get-ItemProperty -LiteralPath $environmentKey -Name Path).Path
        $normalizedBin = $binDir.TrimEnd('\')
        if (-not (@($machinePath -split ';') | Where-Object { $_.TrimEnd('\') -ieq $normalizedBin })) {
            throw "Installer did not add $normalizedBin to the machine PATH."
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
        $installed = $false
        if (Test-Path -LiteralPath $binDir) {
            throw "Uninstaller left $binDir behind."
        }
        $machinePath = (Get-ItemProperty -LiteralPath $environmentKey -Name Path).Path
        if (@($machinePath -split ';') | Where-Object { $_.TrimEnd('\') -ieq $normalizedBin }) {
            throw "Uninstaller did not remove $normalizedBin from the machine PATH."
        }
    } finally {
        if ($installed) {
            $uninstaller = Join-Path $installDir 'unins000.exe'
            if (Test-Path -LiteralPath $uninstaller) {
                Write-Warning 'Cleaning up a failed installer smoke test.'
                Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait | Out-Null
            }
        }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-ExtractedZip {
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][string[]]$DllNames,
        [Parameter(Mandatory = $true)][bool]$RequireSignatures
    )
    $tempRoot = New-ProbeDirectory
    try {
        $extractDir = Join-Path $tempRoot 'extract'
        $workDir = Join-Path $tempRoot 'cwd'
        New-Item -ItemType Directory -Path $extractDir, $workDir | Out-Null
        Expand-Archive -LiteralPath $Paths.AssetPath -DestinationPath $extractDir
        $bundleRoot = Assert-ZipRoot -ExtractDir $extractDir -BaseName $Paths.BaseName
        Assert-BundleRoot -BundleRoot $bundleRoot -DllNames $DllNames
        $binDir = Join-Path $bundleRoot 'bin'
        Assert-ImportClosure -BinDir $binDir
        if ($RequireSignatures) {
            Assert-Authenticode -BinDir $binDir
        }
        Enable-FailFastLoaderErrors
        $forbidden = @(
            $RepoRoot, $extractDir, $bundleRoot, $binDir, $workDir, $Paths.BundleRoot, $Paths.DistDir
        )
        foreach ($exe in $BundleExes) {
            Invoke-VersionProbe -ExePath (Join-Path $binDir $exe) -WorkDir $workDir -ForbiddenRoots $forbidden
        }
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-Assemble {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (Test-Path -LiteralPath $paths.BundleRoot) {
        Remove-Item -LiteralPath $paths.BundleRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $paths.BinDir | Out-Null
    $dlls = @(Get-RequiredRuntimeDlls (Get-BuildExePaths))
    Copy-BundlePayload -BinDir $paths.BinDir -Dlls $dlls
    $dllNames = @($dlls | ForEach-Object { $_.Name })
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames $dllNames
    Assert-ImportClosure -BinDir $paths.BinDir
    Write-Host "Assembled $($paths.BundleRoot)"
}

function Get-RuntimeDllNames {
    return @((Get-RequiredRuntimeDlls (Get-BuildExePaths)) | ForEach-Object { $_.Name })
}

function Invoke-ListSignFiles {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames (Get-RuntimeDllNames)
    Assert-ImportClosure -BinDir $paths.BinDir
    return @(Get-BinariesWeSign -BinDir $paths.BinDir)
}

function Invoke-Package {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    $dllNames = Get-RuntimeDllNames
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames $dllNames
    Assert-ImportClosure -BinDir $paths.BinDir
    Publish-BundleZip -Paths $paths -IsRelease (Require-Flag 'IS_RELEASE')
    Invoke-ExtractedZip -Paths $paths -DllNames $dllNames -RequireSignatures (Require-Flag 'REQUIRE_SIGNATURES')
    Write-Host "Verified $($paths.AssetPath)"
}

if ($MyInvocation.InvocationName -ne '.') {
    switch ($Action) {
        'Assemble' { Invoke-Assemble }
        'ListSignFiles' { Invoke-ListSignFiles }
        'Package' { Invoke-Package }
        'BuildInstaller' { Invoke-BuildInstaller }
        'ListInstallerSignFile' { Invoke-ListInstallerSignFile }
        'FinalizeInstaller' { Invoke-FinalizeInstaller }
        'VerifyInstaller' { Invoke-VerifyInstaller }
        'TestInstaller' { Invoke-TestInstaller }
    }
}
