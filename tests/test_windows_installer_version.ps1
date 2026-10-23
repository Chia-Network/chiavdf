$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $root 'scripts/package-windows-bundle.ps1'

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
    throw 'Get-InstallerVersionInfoVersion was not found in package-windows-bundle.ps1'
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

Write-Host "Windows installer version checks passed ($($cases.Count) cases)"
