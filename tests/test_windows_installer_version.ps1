$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $root 'scripts/windows-installer.ps1'

$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath,
    [ref]$tokens,
    [ref]$errors
)
if ($errors.Count -gt 0) {
    throw ($errors | ForEach-Object { $_.Message }) -join [Environment]::NewLine
}

$functionAst = $ast.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-InstallerVersionInfoVersion'
    }, $true)
if ($null -eq $functionAst) {
    throw 'Get-InstallerVersionInfoVersion was not found in windows-installer.ps1'
}
Invoke-Expression $functionAst.Extent.Text

$cases = @(
    @{ Input = '1.2.3'; Expected = '1.2.3.0' },
    @{ Input = '0.0.1-12345'; Expected = '0.0.1.12345' },
    # GitHub run IDs can exceed Int32; the capped Inno component must not throw.
    @{ Input = '0.0.1-3000000000'; Expected = '0.0.1.65535' },
    @{ Input = '0.0.1-999999999999999999'; Expected = '0.0.1.65535' },
    @{ Input = 'v1.2'; Expected = '1.2.0.0' }
)

foreach ($case in $cases) {
    $actual = Get-InstallerVersionInfoVersion $case.Input
    if ($actual -ne $case.Expected) {
        throw "Get-InstallerVersionInfoVersion('$($case.Input)') returned '$actual', expected '$($case.Expected)'"
    }
}

. (Join-Path $root 'scripts/windows-installer.ps1')
. (Join-Path $root 'scripts/windows-ftdi-driver.ps1')

$required = @(Get-FtdiDriverRequiredFiles)
if ($required.Count -ne 18) {
    throw "FTDI driver required-file list has $($required.Count) entries, expected 18"
}
foreach ($name in @(
        'ftdibus.inf',
        'ftdiport.cat',
        'amd64\ftdibus.sys',
        'i386\ftcserco.dll'
    )) {
    if ($required -notcontains $name) {
        throw "FTDI driver required-file list is missing $name"
    }
}

$expectedCn = 'Microsoft Windows Hardware Compatibility Publisher'
foreach ($subject in @(
        "CN=$expectedCn, OU=Windows, O=Microsoft Corporation, C=US",
        "CN=`"$expectedCn`", O=Microsoft Corporation"
    )) {
    $actual = Get-CertificateSubjectCommonName -Subject $subject
    if ($actual -ne $expectedCn) {
        throw "Get-CertificateSubjectCommonName('$subject') returned '$actual'"
    }
}
$loose = Get-CertificateSubjectCommonName -Subject "CN=Not $expectedCn"
if ($loose -eq $expectedCn) {
    throw 'Get-CertificateSubjectCommonName accepted a loose substring match'
}

$tempInf = [System.IO.Path]::GetTempFileName()
try {
    Set-Content -LiteralPath $tempInf -Value 'USB\VID_0403&PID_601C' -Encoding ASCII
    Assert-FtdiDriverInfVidPid -Path $tempInf
    Set-Content -LiteralPath $tempInf -Value 'USB\VID_1234&PID_5678' -Encoding ASCII
    $threw = $false
    try {
        Assert-FtdiDriverInfVidPid -Path $tempInf
    } catch {
        $threw = $true
    }
    if (-not $threw) {
        throw 'Assert-FtdiDriverInfVidPid accepted a non-FT4222H VID/PID'
    }
} finally {
    Remove-Item -LiteralPath $tempInf -Force -ErrorAction SilentlyContinue
}

$isccDir = Join-Path ([System.IO.Path]::GetTempPath()) ('chiavdf-iscc-' + [guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Path $isccDir | Out-Null
$savedIscc = $env:INNO_SETUP_ISCC
try {
    $env:INNO_SETUP_ISCC = Join-Path $isccDir 'ISCC.exe'
    Set-Content -LiteralPath $env:INNO_SETUP_ISCC -Value 'stub'
    $actual = Get-IsccPath
    if ((Resolve-Path -LiteralPath $actual).Path -ne (Resolve-Path -LiteralPath $env:INNO_SETUP_ISCC).Path) {
        throw "Get-IsccPath returned '$actual' instead of INNO_SETUP_ISCC"
    }
} finally {
    $env:INNO_SETUP_ISCC = $savedIscc
    Remove-Item -LiteralPath $isccDir -Recurse -Force -ErrorAction SilentlyContinue
}

$signTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('chiavdf-sign-' + [guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Path $signTestRoot | Out-Null
$fakeTemp = Join-Path $signTestRoot 'temp'
$fakeRunnerTemp = Join-Path $signTestRoot 'runner-temp'
New-Item -ItemType Directory -Path $fakeTemp, $fakeRunnerTemp | Out-Null
$fakeDlib = Join-Path $signTestRoot 'dlib.dll'
$fakeMetadata = Join-Path $signTestRoot 'metadata.json'
Set-Content -LiteralPath $fakeDlib -Value 'stub'
Set-Content -LiteralPath $fakeMetadata -Value '{}'
$savedEnv = @{}
foreach ($name in @(
        'GITHUB_WORKSPACE', 'RUNNER_TEMP', 'TMPDIR', 'TMP', 'TEMP',
        'ACTIONS_ID_TOKEN_REQUEST_URL', 'ACTIONS_ID_TOKEN_REQUEST_TOKEN',
        'AZURE_TENANT_ID', 'AZURE_CLIENT_ID',
        'AZURE_CODE_SIGNING_DLIB', 'AZURE_CODE_SIGNING_METADATA'
    )) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name)
}
try {
    # Point the temporary roots at directories that do not contain the repo so
    # the helper's path restriction, not the token code, rejects this file.
    $env:GITHUB_WORKSPACE = $root
    $env:RUNNER_TEMP = $fakeRunnerTemp
    $env:TMPDIR = $fakeTemp
    $env:TMP = $fakeTemp
    $env:TEMP = $fakeTemp
    $env:ACTIONS_ID_TOKEN_REQUEST_URL = 'http://127.0.0.1:1/'
    $env:ACTIONS_ID_TOKEN_REQUEST_TOKEN = 'test'
    $env:AZURE_TENANT_ID = 'tenant'
    $env:AZURE_CLIENT_ID = 'client'
    $env:AZURE_CODE_SIGNING_DLIB = $fakeDlib
    $env:AZURE_CODE_SIGNING_METADATA = $fakeMetadata

    $signScript = Join-Path $root 'scripts/sign-windows-file.ps1'
    $thrown = $null
    try {
        & $signScript -Path $signScript
    } catch {
        $thrown = $_.Exception.Message
    }
    if (-not $thrown -or $thrown -notmatch 'Refusing to sign') {
        throw "sign-windows-file.ps1 did not reject a path outside the build outputs before requesting a token: $thrown"
    }
} finally {
    foreach ($name in $savedEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnv[$name])
    }
    Remove-Item -LiteralPath $signTestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows installer checks passed ($($cases.Count) version cases plus driver, ISCC, and signing-path checks)"
