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

$ZipUrl = "https://ftdichip.com/wp-content/uploads/2025/06/LibFT4222-v1.4.8.zip"
$ZipPath = Join-Path $WorkDir "LibFT4222-v1.4.8.zip"
$ExtractDir = Join-Path $WorkDir "extract"

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

function Install-LibFT4222 {
    Ensure-Dir $WorkDir
    if (Test-Path $ExtractDir) {
        Remove-Item $ExtractDir -Recurse -Force
    }

    Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath
    Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force

    $headers = @("libft4222.h", "ftd2xx.h", "WinTypes.h")
    foreach ($header in $headers) {
        $match = Get-ChildItem -Path $ExtractDir -Recurse -Filter $header | Select-Object -First 1
        if (-not $match) {
            throw "Missing required header: $header"
        }
        Copy-Item $match.FullName -Destination (Join-Path $WorkDir $header) -Force
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
