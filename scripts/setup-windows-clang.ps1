# Keep the Python, Rust, and client builds on the same LLVM release.
$ErrorActionPreference = 'Stop'
$llvmVersion = '22.1.8'
$llvmBin = 'C:\Program Files\LLVM\bin'
$clangCl = Join-Path $llvmBin 'clang-cl.exe'
$versionPattern = "\bclang version $([regex]::Escape($llvmVersion))\b"

$version = if (Test-Path $clangCl) { & $clangCl --version }
if (-not ($version -match $versionPattern)) {
    choco upgrade llvm --version=$llvmVersion --allow-downgrade --yes --no-progress
    if ($LASTEXITCODE -ne 0) { throw "LLVM installation failed with exit code $LASTEXITCODE" }
}

$env:PATH = "$llvmBin;$env:PATH"
$llvmBin | Out-File -Append -FilePath $env:GITHUB_PATH
$version = & clang-cl --version
if ($LASTEXITCODE -ne 0 -or -not ($version -match $versionPattern)) {
    throw "Expected clang-cl from LLVM $llvmVersion, got: $version"
}
$version | Write-Host

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    choco install ninja --yes --no-progress
    if ($LASTEXITCODE -ne 0) { throw "Ninja installation failed with exit code $LASTEXITCODE" }
}
