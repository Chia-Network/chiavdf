# Shared FTDI CDM driver package layout and validation.
#
# get-libft4222.ps1 uses this when staging the driver, and the Windows installer
# packaging uses it before compiling. Keeping the file list, the FT4222H VID/PID
# check, and the catalog signer check here prevents the two paths from drifting.

$FtdiDriverTopLevelFiles = @(
    'ftdibus.inf',
    'ftdibus.cat',
    'ftdiport.inf',
    'ftdiport.cat'
)
$FtdiDriverArchDirs = @('amd64', 'i386')
$FtdiDriverPayloadFiles = @(
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
$FtdiDriverInfVidPid = 'USB\VID_0403&PID_601C'
$FtdiDriverCatalogNames = @('ftdibus.cat', 'ftdiport.cat')
$FtdiDriverDefaultSignerCommonName = 'Microsoft Windows Hardware Compatibility Publisher'

function Get-FtdiDriverRequiredFiles {
    return @($FtdiDriverTopLevelFiles + $FtdiDriverPayloadFiles)
}

function Get-CertificateSubjectCommonName {
    param([Parameter(Mandatory = $true)][string]$Subject)
    $match = [regex]::Match($Subject, '(?:^|,\s*)CN=(?:"([^"]*)"|([^,]*))')
    if (-not $match.Success) {
        return $null
    }
    if ($match.Groups[1].Success) {
        return $match.Groups[1].Value
    }
    return $match.Groups[2].Value.Trim()
}

function Assert-FtdiDriverInfVidPid {
    param([Parameter(Mandatory = $true)][string]$Path)
    $inf = Get-Content -LiteralPath $Path -Raw
    if ($inf -notmatch [regex]::Escape($FtdiDriverInfVidPid)) {
        throw 'ftdibus.inf does not declare the FT4222H VID/PID (0403:601c).'
    }
}

function Assert-FtdiDriverSource {
    param([Parameter(Mandatory = $true)][string]$DriverDir)
    if (-not (Test-Path -LiteralPath $DriverDir -PathType Container)) {
        throw "FTDI driver directory is missing: $DriverDir. Run scripts/get-libft4222.ps1 install."
    }
    foreach ($relative in Get-FtdiDriverRequiredFiles) {
        $path = Join-Path $DriverDir $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "FTDI driver file is missing: $path"
        }
    }
    Assert-FtdiDriverInfVidPid -Path (Join-Path $DriverDir 'ftdibus.inf')
    $expectedSigner = [Environment]::GetEnvironmentVariable('EXPECTED_DRIVER_SIGNER_CN')
    if ([string]::IsNullOrWhiteSpace($expectedSigner)) {
        $expectedSigner = $FtdiDriverDefaultSignerCommonName
    }
    foreach ($catalog in $FtdiDriverCatalogNames) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $DriverDir $catalog)
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "$catalog has no valid Authenticode signature: $($signature.StatusMessage)"
        }
        if ($null -eq $signature.SignerCertificate) {
            throw "$catalog has no signer certificate."
        }
        $commonName = Get-CertificateSubjectCommonName -Subject $signature.SignerCertificate.Subject
        if (-not [string]::Equals($commonName, $expectedSigner, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "$catalog signer CN '$commonName' does not match '$expectedSigner'."
        }
    }
}
