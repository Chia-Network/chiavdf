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

Write-Host "Windows installer checks passed ($($cases.Count) version cases plus driver helper checks)"
