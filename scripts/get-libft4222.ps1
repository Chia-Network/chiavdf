Param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("install", "clean")]
    [string]$Action = "install"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "windows-pe.ps1")
. (Join-Path $PSScriptRoot "windows-ftdi-driver.ps1")

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$SrcDir = Join-Path $RootDir "src"
$WorkDir = Join-Path $SrcDir "libft4222"
$HwDir = Join-Path $SrcDir "hw\libft4222"

$ZipUrl = "https://download.chia.net/vdf/LibFT4222-v1.4.8.zip"
# ftdichip.cn is currently down; confirm this URL before enabling it as a fallback:
# $ZipUrl = "https://ftdichip.cn/Support/SoftwareExamples/LibFT4222-v1.4.8.zip"
$ZipSha256 = "f7bab9e78ec15035e0dd43be9043bc19557eff558d91e4854bb1d4f52d5396cd"
$ZipPath = Join-Path $WorkDir "LibFT4222-v1.4.8.zip"
$ExtractDir = Join-Path $WorkDir "extract"

# LibFT4222 1.4.8 ships the amd64 import library and LibFT4222-64.dll, but
# not the native D2XX DLL that import library names. The DLL comes from
# FTDI CDM 2.12.36.20, which stores it as amd64/FTD2XX64.dll. The archive is
# mirrored at download.chia.net and tried first, with FTDI's ftdichip.cn
# mirror as the fallback. The SHA-256 pins the bytes whichever host serves
# them.
# New driver versions can be checked at https://ftdichip.com/drivers/d2xx-drivers/
#
# The same CDM archive supplies the installer's unmodified ftdibus.inf/
# ftdiport.inf driver package. Staging it here keeps the DLL and driver from
# the same pinned archive and lets the signing job consume both from the build
# artifact instead of downloading the driver again.
$D2xxUrls = @(
    "https://download.chia.net/vdf/CDM-v2.12.36.20-WHQL-Certified.zip"
    "https://ftdichip.cn/Drivers/CDM/CDM-v2.12.36.20-WHQL-Certified.zip"
)
$D2xxSha256 = "e8ebf401d06994183694d78e5855ed5ab026bdfa8c3d4e4ccde09ec238a0eaf1"
$D2xxZipPath = Join-Path $WorkDir "CDM-v2.12.36.20-WHQL-Certified.zip"
$D2xxEntry = "amd64/FTD2XX64.dll"
$D2xxStagedName = "FTD2XX.dll"
$D2xxDriverExtractDir = Join-Path $WorkDir "cdm-extract"

$LibFt4222Dll = "imports\LibFT4222\dll\amd64\LibFT4222-64.dll"
$LibFt4222Lib = "imports\LibFT4222\dll\amd64\LibFT4222-64.lib"
$Ftd2xxLib = "imports\ftd2xx\dll\amd64\ftd2xx.lib"

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Ensure-Dir {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Find-Header {
    param(
        [string]$BaseName,
        [string[]]$Alternates = @()
    )
    $names = @($BaseName) + $Alternates
    foreach ($name in $names) {
        $match = Get-ChildItem -Path $ExtractDir -Recurse -File |
            Where-Object { $_.Name -ieq $name } |
            Select-Object -First 1
        if ($match) {
            return $match
        }
    }
    return $null
}

function Test-ZipValid {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        return $false
    }
    $zip = $null
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
        $null = $zip.Entries.Count
        return $true
    } catch {
        return $false
    } finally {
        if ($zip) {
            $zip.Dispose()
        }
    }
}

function Download-With-Retry {
    param(
        [string]$Url,
        [string]$Path,
        [string]$Sha256,
        [int]$Attempts = 3
    )
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        if (Test-Path $Path) {
            Remove-Item $Path -Force
        }
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Path -MaximumRedirection 5 -UserAgent 'chiavdf-windows-deps'
        } catch {
            if ($attempt -eq $Attempts) {
                throw
            }
            Start-Sleep -Seconds (2 * $attempt)
            continue
        }
        if ((Test-ZipValid $Path) -and ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha256)) {
            return
        }
        Write-Warning "Downloaded zip failed validation (attempt $attempt/$Attempts)."
        Start-Sleep -Seconds (2 * $attempt)
    }
    throw "Failed to download a valid zip from $Url after $Attempts attempts."
}

function Download-FromMirrors {
    param(
        [string[]]$Urls,
        [string]$Path,
        [string]$Sha256,
        [int]$Attempts = 3
    )
    foreach ($url in $Urls) {
        try {
            $null = Download-With-Retry -Url $url -Path $Path -Sha256 $Sha256 -Attempts $Attempts
            return $url
        } catch {
            Write-Warning "Failed to download a valid zip from $url."
        }
    }
    throw "Failed to download a valid zip from any mirror: $($Urls -join ', ')."
}

function Copy-ExtractedFile {
    param(
        [string]$RelativePath,
        [string]$Destination
    )
    $source = Join-Path $ExtractDir $RelativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing required FT4222 file: $RelativePath"
    }
    Copy-Item -LiteralPath $source -Destination $Destination -Force
}

function Install-Ftd2xxDll {
    $script:D2xxSourceUrl = Download-FromMirrors -Urls $D2xxUrls -Path $D2xxZipPath -Sha256 $D2xxSha256 -Attempts 3
    $zip = [System.IO.Compression.ZipFile]::OpenRead($D2xxZipPath)
    try {
        $entry = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $D2xxEntry })
        if ($entry.Count -ne 1) {
            throw "CDM archive does not contain $D2xxEntry."
        }
        $destination = Join-Path $WorkDir $D2xxStagedName
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry[0], $destination, $true)
    } finally {
        $zip.Dispose()
    }
    Assert-NativeAmd64Dll (Join-Path $WorkDir $D2xxStagedName) | Out-Null
}

function Install-Ftd2xxDriver {
    param([string]$Destination)

    if (Test-Path $D2xxDriverExtractDir) {
        Remove-Item $D2xxDriverExtractDir -Recurse -Force
    }
    Expand-Archive -Path $D2xxZipPath -DestinationPath $D2xxDriverExtractDir -Force
    if (Test-Path $Destination) {
        Remove-Item $Destination -Recurse -Force
    }
    Ensure-Dir $Destination

    foreach ($name in $FtdiDriverTopLevelFiles) {
        $source = Join-Path $D2xxDriverExtractDir $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "CDM archive is missing $name."
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $name) -Force
    }
    foreach ($arch in $FtdiDriverArchDirs) {
        $source = Join-Path $D2xxDriverExtractDir $arch
        if (-not (Test-Path -LiteralPath $source -PathType Container)) {
            throw "CDM archive is missing the $arch driver directory."
        }
        $target = Join-Path $Destination $arch
        Ensure-Dir $target
        # The driver package needs the DLL/SYS payloads, not the import
        # libraries that live beside them in the CDM archive.
        Get-ChildItem -LiteralPath $source -File |
            Where-Object { $_.Extension -ne '.lib' } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $target -Force }
    }

    Assert-FtdiDriverSource -DriverDir $Destination

    @"
FTDI CDM 2.12.36.20 (WHQL Certified)
Source: $script:D2xxSourceUrl
SHA-256: $D2xxSha256
License: see the unmodified ftdibus.inf and ftdiport.inf headers.
"@ | Set-Content -LiteralPath (Join-Path $Destination "SOURCE.txt") -Encoding ASCII

    Remove-Item $D2xxDriverExtractDir -Recurse -Force -ErrorAction SilentlyContinue
}

function Install-LibFT4222 {
    Ensure-Dir $WorkDir
    if (Test-Path $ExtractDir) {
        Remove-Item $ExtractDir -Recurse -Force
    }

    Download-With-Retry -Url $ZipUrl -Path $ZipPath -Sha256 $ZipSha256 -Attempts 3
    Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force

    $headers = @(
        @{ Name = "libft4222.h"; Alternates = @() },
        @{ Name = "ftd2xx.h"; Alternates = @() },
        @{ Name = "WinTypes.h"; Alternates = @("wintypes.h") }
    )
    foreach ($header in $headers) {
        $match = Find-Header -BaseName $header.Name -Alternates $header.Alternates
        if (-not $match) {
            if ($header.Name -eq "WinTypes.h") {
                $stubPath = Join-Path $WorkDir $header.Name
                @"
#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
"@ | Set-Content -Path $stubPath -Encoding ASCII
                continue
            }
            throw "Missing required header: $($header.Name)"
        }
        Copy-Item $match.FullName -Destination (Join-Path $WorkDir $header.Name) -Force
    }

    Copy-ExtractedFile $LibFt4222Dll (Join-Path $WorkDir "LibFT4222-64.dll")
    Copy-ExtractedFile $LibFt4222Lib (Join-Path $WorkDir "LibFT4222-64.lib")
    Copy-ExtractedFile $Ftd2xxLib (Join-Path $WorkDir "ftd2xx.lib")
    Assert-NativeAmd64Dll (Join-Path $WorkDir "LibFT4222-64.dll") | Out-Null
    Install-Ftd2xxDll

    if (Test-Path $HwDir) {
        Remove-Item $HwDir -Recurse -Force
    }
    Ensure-Dir $HwDir
    Install-Ftd2xxDriver -Destination (Join-Path $HwDir "driver")

    foreach ($name in @(
            "libft4222.h",
            "ftd2xx.h",
            "WinTypes.h",
            "LibFT4222-64.lib",
            "LibFT4222-64.dll",
            "ftd2xx.lib",
            "FTD2XX.dll"
        )) {
        Copy-Item (Join-Path $WorkDir $name) -Destination (Join-Path $HwDir $name) -Force
    }
}

function Clean-LibFT4222 {
    if (Test-Path $WorkDir) {
        Remove-Item $WorkDir -Recurse -Force
    }
    if (Test-Path $HwDir) {
        Remove-Item $HwDir -Recurse -Force
    }
}

switch ($Action) {
    "install" { Install-LibFT4222 }
    "clean" { Clean-LibFT4222 }
}
