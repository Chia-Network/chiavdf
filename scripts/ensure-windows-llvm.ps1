# Make LLVM/clang 22+ available on a GitHub Windows runner.
#
# The windows-latest image ships Visual Studio 18, whose STL (MSVC 14.52)
# refuses Clang older than 22 (yvals_core.h, error STL1000). That breaks
# bindgen's libclang (Rust bindings) and clang-cl builds, and the image's own
# LLVM is older. Chocolatey is tried first (retried: its feed sometimes returns
# 503 while choco still reports success). If LLVM is still too old, the
# official LLVM MSI (pinned, sha256 from the GitHub release) is extracted into
# its own directory, so the image's LLVM cannot shadow it.
#
# Exports for later steps: LLVM_BIN and LIBCLANG_PATH (GITHUB_ENV), and the
# LLVM bin directory first on PATH (GITHUB_PATH).
$ErrorActionPreference = 'Stop'

$MinMajor = 22
$MsiUrl = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/LLVM-23.1.3-win64.msi'
$MsiSha256 = '5153f848ac87553118340f16f8d3cd4351d729bcba6e4785c6a4a9af29676de9'

function Get-ClangMajor([string]$bin) {
    $exe = Join-Path $bin 'clang-cl.exe'
    if (-not (Test-Path $exe)) { return 0 }
    $line = (& $exe --version | Select-Object -First 1)
    Write-Host "clang-cl in ${bin}: $line"
    if ($line -match 'version (\d+)\.') { return [int]$Matches[1] }
    return 0
}

$llvmBin = 'C:\Program Files\LLVM\bin'
for ($attempt = 1; $attempt -le 3 -and (Get-ClangMajor $llvmBin) -lt $MinMajor; $attempt++) {
    Write-Host "choco upgrade llvm (attempt $attempt)"
    choco upgrade llvm --yes --no-progress
    if ((Get-ClangMajor $llvmBin) -lt $MinMajor) { Start-Sleep -Seconds (20 * $attempt) }
}

if ((Get-ClangMajor $llvmBin) -lt $MinMajor) {
    Write-Host "Chocolatey did not provide LLVM ${MinMajor}+; extracting the LLVM MSI instead"
    $msi = Join-Path $env:RUNNER_TEMP 'llvm.msi'
    Invoke-WebRequest -Uri $MsiUrl -OutFile $msi -MaximumRetryCount 3 -RetryIntervalSec 10
    $hash = (Get-FileHash $msi -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $MsiSha256) { throw "LLVM MSI sha256 mismatch: $hash" }
    $dest = Join-Path $env:RUNNER_TEMP 'llvm'
    $p = Start-Process msiexec.exe -ArgumentList @('/a', "`"$msi`"", '/qn', "TARGETDIR=`"$dest`"") -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "msiexec /a failed with exit code $($p.ExitCode)" }
    $exe = Get-ChildItem $dest -Recurse -Filter clang-cl.exe | Select-Object -First 1
    if (-not $exe) { throw "clang-cl.exe not found in extracted LLVM MSI" }
    $llvmBin = $exe.DirectoryName
}

$major = Get-ClangMajor $llvmBin
if ($major -lt $MinMajor) { throw "LLVM $major is too old for this MSVC STL (needs ${MinMajor}+)" }

$llvmBin | Out-File -Append -FilePath $env:GITHUB_PATH
"LLVM_BIN=$llvmBin" | Out-File -Append -FilePath $env:GITHUB_ENV
# bindgen (clang-sys) loads libclang.dll from here.
"LIBCLANG_PATH=$llvmBin" | Out-File -Append -FilePath $env:GITHUB_ENV
Write-Host "Using LLVM $major from $llvmBin"
