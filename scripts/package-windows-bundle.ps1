# Build the Windows release zip so it starts on a machine that has never
# seen this checkout.
#
# Windows loads a DLL from the directory containing the executable, then
# System32, the Windows directory, the current directory, and PATH. It does
# not look in a sibling lib directory. MPIR's proxy also loads its CPU
# variant from the directory that contains mpir.dll. Every shipped exe and
# DLL therefore lives in bin/, and the zip is launched from an empty
# directory with a PATH that contains only the system directories.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Assemble', 'Sign', 'Package')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

$BundleExes = @(
    'vdf_client.exe',
    'vdf_bench.exe',
    'hw_vdf_client.exe',
    'emu_hw_vdf_client.exe',
    'hw_test.exe',
    'emu_hw_test.exe'
)

# Floor for the two source trees. Get-SourceDlls also ships every other DLL
# those directories contain, and the zip must match that set exactly.
$RequiredDllNames = @(
    'mpir.dll',
    'mpir_gc.dll',
    'mpir_sandybridge.dll',
    'mpir_haswell.dll',
    'mpir_broadwell.dll',
    'mpir_broadwell_avx.dll',
    'mpir_skylake_avx.dll',
    'mpir_bulldozer.dll',
    'mpir_piledriver.dll',
    'libft4222.dll',
    'ftd2xx.dll'
)

$DllSourceDirs = @(
    (Join-Path $RepoRoot 'mpir_gc_x64'),
    (Join-Path $RepoRoot 'src/hw/libft4222')
)

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
    [pscustomobject]@{
        BaseName        = $baseName
        DistDir         = $distDir
        BundleRoot      = $bundleRoot
        BinDir          = Join-Path $bundleRoot 'bin'
        AssetPath       = Join-Path $distDir "$baseName.zip"
        WingetAssetPath = Join-Path $distDir 'chiavdf-win64.zip'
    }
}

function Get-SourceDlls {
    $found = foreach ($dir in $DllSourceDirs) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
            throw "DLL source directory is missing: $dir"
        }
        $dlls = @(Get-ChildItem -LiteralPath $dir -Filter '*.dll' -File)
        if ($dlls.Count -eq 0) {
            throw "DLL source directory contains no DLLs: $dir"
        }
        $dlls
    }
    $found = @($found)
    $names = @($found | ForEach-Object { $_.Name })
    $duplicates = @($names | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    if ($duplicates.Count -gt 0) {
        throw "Duplicate runtime DLL names: $($duplicates -join ', ')"
    }
    $missing = @($RequiredDllNames | Where-Object { $names -notcontains $_ })
    if ($missing.Count -gt 0) {
        throw "Required runtime DLLs were not found beside their sources: $($missing -join ', ')"
    }
    return $found
}

function Get-DllNames {
    param([Parameter(Mandatory = $true)]$Dlls)
    return @($Dlls | ForEach-Object { $_.Name } | Sort-Object)
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
    $actual = @($binFiles | ForEach-Object { $_.Name } | Sort-Object)
    $expected = @($BundleExes + $DllNames | Sort-Object -Unique)
    $missing = @($expected | Where-Object { $actual -notcontains $_ })
    $extra = @($actual | Where-Object { $expected -notcontains $_ })
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
        Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $BinDir $dll.Name) -Force
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

function Sign-ShippedBinaries {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    $thumbprint = Require-Text 'SM_CODE_SIGNING_CERT_SHA1_HASH'
    if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) {
        throw 'signtool.exe is not on PATH.'
    }
    foreach ($binary in @(Get-ShippedBinaries $BinDir)) {
        & signtool.exe sign /sha1 $thumbprint /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 $binary.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "signtool sign failed for $($binary.FullName) with exit code $LASTEXITCODE"
        }
        & signtool.exe verify /v /pa $binary.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "signtool verify failed for $($binary.FullName) with exit code $LASTEXITCODE"
        }
        Write-Host "Signed $($binary.Name)"
    }
}

function Assert-Authenticode {
    param([Parameter(Mandatory = $true)][string]$BinDir)
    if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) {
        throw 'Signing was requested, but signtool.exe is not on PATH. Refusing to publish the zip.'
    }
    foreach ($binary in @(Get-ShippedBinaries $BinDir)) {
        & signtool.exe verify /v /pa $binary.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "Shipped binary is unsigned or its signature is invalid: $($binary.FullName)"
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
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string[]]$ForbiddenRoots
    )
    if ($PathValue -ne $Expected) {
        throw "Launch PATH is not limited to the system directories. Refusing to run. PATH=$PathValue"
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
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$WorkDir
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
    $psi.EnvironmentVariables['Path'] = $PathValue
    $applied = $psi.EnvironmentVariables['Path']
    if ($applied -ne $PathValue) {
        throw "Failed to restrict PATH for ${leaf}. Applied PATH=$applied"
    }

    Write-Host "Launching $leaf --version"
    Write-Host "  exe: $ExePath"
    Write-Host "  cwd: $WorkDir"
    Write-Host "  PATH: $applied"

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
        return
    }
    Copy-Item -LiteralPath $Paths.AssetPath -Destination $Paths.WingetAssetPath -Force
    Set-Content -LiteralPath "$($Paths.WingetAssetPath).sha256" -Value $hash -NoNewline
    $aliasHash = (Get-FileHash -LiteralPath $Paths.WingetAssetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($aliasHash -ne $hash) {
        throw 'chiavdf-win64.zip does not match the versioned Windows zip.'
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
        if ($RequireSignatures) {
            Assert-Authenticode -BinDir $binDir
        }
        $systemPath = Get-SystemOnlyPath
        Assert-SystemOnlyPath -PathValue $systemPath -Expected $systemPath -ForbiddenRoots @(
            $RepoRoot, $extractDir, $bundleRoot, $binDir, $workDir, $Paths.BundleRoot, $Paths.DistDir
        )
        Enable-FailFastLoaderErrors
        foreach ($exe in $BundleExes) {
            Invoke-VersionProbe -ExePath (Join-Path $binDir $exe) -PathValue $systemPath -WorkDir $workDir
        }
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-Assemble {
    $signingRequested = Require-Flag 'REQUEST_WINDOWS_SIGNING'
    if ($signingRequested) {
        if (-not (Require-Flag 'HAS_WINDOWS_SIGNING_SECRET')) {
            throw 'Windows signing was requested but signing secrets (SM_*) are not configured.'
        }
    } else {
        Write-Host 'Windows signing not requested; the bundle will be published unsigned.'
    }
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    if (Test-Path -LiteralPath $paths.BundleRoot) {
        Remove-Item -LiteralPath $paths.BundleRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $paths.BinDir | Out-Null
    $dlls = @(Get-SourceDlls)
    Copy-BundlePayload -BinDir $paths.BinDir -Dlls $dlls
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames (Get-DllNames $dlls)
    Write-Host "Assembled $($paths.BundleRoot)"
}

function Invoke-Sign {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    $dllNames = Get-DllNames (Get-SourceDlls)
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames $dllNames
    Sign-ShippedBinaries -BinDir $paths.BinDir
}

function Invoke-Package {
    $paths = Get-BundlePaths (Require-Text 'INSTALLER_VERSION')
    $dllNames = Get-DllNames (Get-SourceDlls)
    Assert-BundleRoot -BundleRoot $paths.BundleRoot -DllNames $dllNames
    Publish-BundleZip -Paths $paths -IsRelease (Require-Flag 'IS_RELEASE')
    Invoke-ExtractedZip -Paths $paths -DllNames $dllNames -RequireSignatures (Require-Flag 'REQUEST_WINDOWS_SIGNING')
    Write-Host "Verified $($paths.AssetPath)"
}

if ($MyInvocation.InvocationName -ne '.') {
    switch ($Action) {
        'Assemble' { Invoke-Assemble }
        'Sign' { Invoke-Sign }
        'Package' { Invoke-Package }
    }
}
