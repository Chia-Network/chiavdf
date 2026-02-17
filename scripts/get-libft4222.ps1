Param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("install", "clean")]
    [string]$Action = "install"
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$SrcDir = Join-Path $RootDir "src"
$WorkDir = Join-Path $SrcDir "libft4222"
$HwDir = Join-Path $SrcDir "hw\libft4222"

$ZipUrl = "https://download.chia.net/vdf/LibFT4222-v1.4.8.zip"
$ZipPath = Join-Path $WorkDir "LibFT4222-v1.4.8.zip"
$ExtractDir = Join-Path $WorkDir "extract"

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Ensure-Dir {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Choose-Best {
    param([System.IO.FileInfo[]]$Items)
    if (-not $Items -or $Items.Count -eq 0) {
        return $null
    }
    $preferred = $Items | Where-Object { $_.FullName -match "(x64|amd64|win64)" }
    if ($preferred -and $preferred.Count -gt 0) {
        return $preferred[0]
    }
    return $Items[0]
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
        [int]$Attempts = 3
    )
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        if (Test-Path $Path) {
            Remove-Item $Path -Force
        }
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Path -MaximumRedirection 5
        } catch {
            if ($attempt -eq $Attempts) {
                throw
            }
            Start-Sleep -Seconds (2 * $attempt)
            continue
        }
        if (Test-ZipValid $Path) {
            return
        }
        Write-Warning "Downloaded zip failed validation (attempt $attempt/$Attempts)."
        Start-Sleep -Seconds (2 * $attempt)
    }
    throw "Failed to download a valid zip from $Url after $Attempts attempts."
}

function Install-LibFT4222 {
    Ensure-Dir $WorkDir
    if (Test-Path $ExtractDir) {
        Remove-Item $ExtractDir -Recurse -Force
    }

    Download-With-Retry -Url $ZipUrl -Path $ZipPath -Attempts 3
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

    $libftLib = Choose-Best (Get-ChildItem -Path $ExtractDir -Recurse -Filter "libft4222*.lib")
    $ftdLib = Choose-Best (Get-ChildItem -Path $ExtractDir -Recurse -Filter "ftd2xx*.lib")
    $libftDll = Choose-Best (Get-ChildItem -Path $ExtractDir -Recurse -Filter "libft4222*.dll")
    $ftdDll = Choose-Best (Get-ChildItem -Path $ExtractDir -Recurse -Filter "ftd2xx*.dll")

    if (-not $libftLib -or -not $ftdLib) {
        throw "Missing required .lib files in extracted package"
    }
    if (-not $libftDll -or -not $ftdDll) {
        throw "Missing required .dll files in extracted package"
    }

    Copy-Item $libftLib.FullName -Destination (Join-Path $WorkDir "libft4222.lib") -Force
    Copy-Item $ftdLib.FullName -Destination (Join-Path $WorkDir "ftd2xx.lib") -Force
    Copy-Item $libftDll.FullName -Destination (Join-Path $WorkDir "libft4222.dll") -Force
    Copy-Item $ftdDll.FullName -Destination (Join-Path $WorkDir "ftd2xx.dll") -Force

    if (Test-Path $HwDir) {
        Remove-Item $HwDir -Recurse -Force
    }
    Ensure-Dir $HwDir

    Copy-Item (Join-Path $WorkDir "*.h") -Destination $HwDir -Force
    Copy-Item (Join-Path $WorkDir "*.lib") -Destination $HwDir -Force
    Copy-Item (Join-Path $WorkDir "*.dll") -Destination $HwDir -Force
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
