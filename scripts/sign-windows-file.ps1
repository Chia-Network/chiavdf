# Sign one file with Azure Artifact Signing.
#
# Inno Setup calls this through its SignTool mechanism when the installer is
# compiled with a signed uninstaller. The release workflow installs the Azure
# signing client first (Chia-Network/actions/sign/azure-windows with
# sign: "false"), then passes this script to ISCC as /SChiaSignTool=... $f.
# Signing is restricted to build outputs: files under the workspace dist
# directory or the Inno Setup temporary directory.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-PathUnder {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root
    )
    if ([string]::IsNullOrWhiteSpace($Root)) {
        return $false
    }
    $separators = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd($separators)
    $candidatePath = [System.IO.Path]::GetFullPath($Candidate)
    if ($candidatePath.Equals($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    foreach ($separator in $separators) {
        if ($candidatePath.StartsWith($rootPath + $separator, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

$resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath
if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
    throw "File to sign does not exist: $Path"
}

$allowedRoots = @()
if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_WORKSPACE)) {
    $allowedRoots += (Join-Path $env:GITHUB_WORKSPACE 'dist')
}
$allowedRoots += [System.IO.Path]::GetTempPath()
if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    $allowedRoots += $env:RUNNER_TEMP
}
$allowed = $false
foreach ($root in $allowedRoots) {
    if (Test-PathUnder -Candidate $resolvedPath -Root $root) {
        $allowed = $true
        break
    }
}
if (-not $allowed) {
    throw "Refusing to sign a file outside the build output directories: $resolvedPath"
}
$Path = $resolvedPath
if (-not $env:ACTIONS_ID_TOKEN_REQUEST_URL -or -not $env:ACTIONS_ID_TOKEN_REQUEST_TOKEN) {
    throw 'ACTIONS_ID_TOKEN_REQUEST_URL/TOKEN not available; ensure permissions.id-token: write is set.'
}
if (-not $env:AZURE_TENANT_ID -or -not $env:AZURE_CLIENT_ID) {
    throw 'AZURE_TENANT_ID and AZURE_CLIENT_ID must be set for AZURE_TOKEN_CREDENTIALS=prod.'
}
if (-not $env:AZURE_CODE_SIGNING_DLIB -or -not (Test-Path -LiteralPath $env:AZURE_CODE_SIGNING_DLIB)) {
    throw 'AZURE_CODE_SIGNING_DLIB is not set or does not exist.'
}
if (-not $env:AZURE_CODE_SIGNING_METADATA -or -not (Test-Path -LiteralPath $env:AZURE_CODE_SIGNING_METADATA)) {
    throw 'AZURE_CODE_SIGNING_METADATA is not set or does not exist.'
}

$timestampServer = $env:TIMESTAMP_SERVER
if ([string]::IsNullOrWhiteSpace($timestampServer)) {
    $timestampServer = 'http://timestamp.acs.microsoft.com'
}
$env:AZURE_TOKEN_CREDENTIALS = 'prod'
$tokenFile = Join-Path $env:RUNNER_TEMP ("azure-federated-token-" + [guid]::NewGuid().ToString('n'))
try {
    try {
        $tokenUrl = "$($env:ACTIONS_ID_TOKEN_REQUEST_URL)&audience=api://AzureADTokenExchange"
        $headers = @{ Authorization = "Bearer $($env:ACTIONS_ID_TOKEN_REQUEST_TOKEN)" }
        $response = Invoke-RestMethod -Uri $tokenUrl -Headers $headers -Method GET
        if (-not $response.value) {
            throw 'empty token'
        }
        Set-Content -Path $tokenFile -Value $response.value -NoNewline
        $response = $null
    } catch {
        throw 'Failed to obtain GitHub OIDC token for Azure federated credential.'
    }

    $env:AZURE_FEDERATED_TOKEN_FILE = $tokenFile
    Write-Host "Signing $Path"
    & signtool.exe sign /fd SHA256 /tr $timestampServer /td SHA256 `
        /dlib $env:AZURE_CODE_SIGNING_DLIB `
        /dmdf $env:AZURE_CODE_SIGNING_METADATA `
        $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Azure Artifact Signing failed for $Path with exit code $LASTEXITCODE"
    }
    & signtool.exe verify /pa $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Signature verification failed for $Path"
    }
} finally {
    $env:AZURE_FEDERATED_TOKEN_FILE = $null
    if (Test-Path -LiteralPath $tokenFile) {
        Set-Content -Path $tokenFile -Value '' -NoNewline
        Remove-Item -LiteralPath $tokenFile -Force -ErrorAction SilentlyContinue
    }
}
