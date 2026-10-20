# Read a Windows PE import table. Both the FTDI installer and the release
# zip use this so a managed assembly cannot be staged under a native DLL name.

function Read-U16 {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or ($Offset + 2) -gt $Bytes.Length) {
        throw 'PE is truncated.'
    }
    return [uint16](([uint32]$Bytes[$Offset]) -bor (([uint32]$Bytes[$Offset + 1]) -shl 8))
}

function Read-U32 {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or ($Offset + 4) -gt $Bytes.Length) {
        throw 'PE is truncated.'
    }
    return [uint32](
        ([uint64]$Bytes[$Offset]) -bor
        (([uint64]$Bytes[$Offset + 1]) -shl 8) -bor
        (([uint64]$Bytes[$Offset + 2]) -shl 16) -bor
        (([uint64]$Bytes[$Offset + 3]) -shl 24)
    )
}

function Read-AsciiZ {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Offset -ge $Bytes.Length) {
        throw 'PE string is outside the file.'
    }
    $end = $Offset
    while ($end -lt $Bytes.Length -and $Bytes[$end] -ne 0) {
        $end++
    }
    if ($end -ge $Bytes.Length) {
        throw 'PE string is not terminated.'
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $end - $Offset)
}

function Read-PeImage {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PE is missing: $Path"
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or (Read-U16 $bytes 0) -ne 0x5A4D) {
        throw "$Path is not a PE."
    }
    $lfanew = [int](Read-U32 $bytes 0x3C)
    if ((Read-U32 $bytes $lfanew) -ne 0x4550) {
        throw "$Path is not a PE."
    }
    $machine = Read-U16 $bytes ($lfanew + 4)
    $sectionCount = Read-U16 $bytes ($lfanew + 6)
    $optionalSize = Read-U16 $bytes ($lfanew + 20)
    $optional = $lfanew + 24
    $magic = Read-U16 $bytes $optional
    if ($magic -eq 0x20B) {
        $dataDirectoryCount = Read-U32 $bytes ($optional + 108)
        $dataDirectory = $optional + 112
    } elseif ($magic -eq 0x10B) {
        $dataDirectoryCount = Read-U32 $bytes ($optional + 92)
        $dataDirectory = $optional + 96
    } else {
        throw "$Path has an unknown PE optional header ($magic)."
    }
    $sectionTable = $optional + $optionalSize
    $sections = @()
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $entry = $sectionTable + ($i * 40)
        $sections += [pscustomobject]@{
            VirtualAddress = Read-U32 $bytes ($entry + 12)
            VirtualSize    = Read-U32 $bytes ($entry + 8)
            RawAddress     = Read-U32 $bytes ($entry + 20)
            RawSize        = Read-U32 $bytes ($entry + 16)
        }
    }

    function Convert-Rva([uint32]$Rva) {
        foreach ($section in $sections) {
            $span = $section.VirtualSize
            if ($span -lt $section.RawSize) {
                $span = $section.RawSize
            }
            $start = $section.VirtualAddress
            if ($Rva -ge $start -and $Rva -lt ($start + $span)) {
                return [int]($section.RawAddress + ($Rva - $start))
            }
        }
        throw "$Path has an RVA outside its sections: 0x$($Rva.ToString('X'))"
    }

    function Read-Directory([int]$Index) {
        if ($Index -ge [int]$dataDirectoryCount) {
            return [pscustomobject]@{ Rva = [uint32]0; Size = [uint32]0 }
        }
        $at = [int]$dataDirectory + ($Index * 8)
        return [pscustomobject]@{
            Rva  = (Read-U32 $bytes $at)
            Size = (Read-U32 $bytes ($at + 4))
        }
    }

    function Read-ImportNames([uint32]$Rva) {
        $names = [System.Collections.Generic.List[string]]::new()
        if ($Rva -eq 0) {
            return ,$names
        }
        $offset = Convert-Rva $Rva
        while ($true) {
            $nameRva = Read-U32 $bytes ($offset + 12)
            if ($nameRva -eq 0) {
                break
            }
            $names.Add((Read-AsciiZ $bytes (Convert-Rva $nameRva)))
            $offset += 20
        }
        return ,$names
    }

    function Read-DelayImportNames([uint32]$Rva, [uint32]$Size) {
        $names = [System.Collections.Generic.List[string]]::new()
        if ($Rva -eq 0 -or $Size -eq 0) {
            return ,$names
        }
        $offset = Convert-Rva $Rva
        $end = $offset + [int]$Size
        while (($offset + 32) -le $end) {
            $nameRva = Read-U32 $bytes ($offset + 4)
            if ($nameRva -eq 0) {
                break
            }
            $names.Add((Read-AsciiZ $bytes (Convert-Rva $nameRva)))
            $offset += 32
        }
        return ,$names
    }

    $importDirectory = Read-Directory 1
    $delayDirectory = Read-Directory 13
    $clrDirectory = Read-Directory 14
    $imports = [System.Collections.Generic.List[string]]::new()
    $imports.AddRange((Read-ImportNames $importDirectory.Rva))
    $imports.AddRange((Read-DelayImportNames $delayDirectory.Rva $delayDirectory.Size))
    [pscustomobject]@{
        Path       = $Path
        Machine    = $machine
        IsAmd64    = ($machine -eq 0x8664)
        HasClr     = ($clrDirectory.Size -ne 0)
        Imports    = $imports
    }
}

function Assert-NativeAmd64Dll {
    param([Parameter(Mandatory = $true)][string]$Path)
    $image = Read-PeImage $Path
    if (-not $image.IsAmd64) {
        throw "$Path is not a native AMD64 PE (machine 0x$($image.Machine.ToString('X')))."
    }
    if ($image.HasClr) {
        throw "$Path has a CLR header and cannot be shipped."
    }
    foreach ($import in @($image.Imports)) {
        if ([System.IO.Path]::GetFileName($import) -eq 'mscoree.dll') {
            throw "$Path imports mscoree.dll and cannot be shipped."
        }
    }
    return $image
}
