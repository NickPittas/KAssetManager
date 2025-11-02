# Download Everything SDK for KAssetManager
# This script downloads the Everything SDK and extracts the DLL to third_party/everything/

param(
    [string]$SdkUrl = "https://www.voidtools.com/Everything-SDK.zip",
    [string]$TargetDir = "third_party/everything"
)

$ErrorActionPreference = "Stop"

Write-Host "=== Everything SDK Downloader ===" -ForegroundColor Cyan
Write-Host ""

# Get repository root (parent of scripts directory)
$RepoRoot = Split-Path -Parent $PSScriptRoot
$TargetPath = Join-Path $RepoRoot $TargetDir
$TempZip = Join-Path $env:TEMP "Everything-SDK.zip"
$TempExtract = Join-Path $env:TEMP "Everything-SDK-Extract"

Write-Host "Repository root: $RepoRoot" -ForegroundColor Gray
Write-Host "Target directory: $TargetPath" -ForegroundColor Gray
Write-Host ""

# Create target directory if it doesn't exist
if (-not (Test-Path $TargetPath)) {
    Write-Host "Creating target directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $TargetPath -Force | Out-Null
}

# Download SDK
Write-Host "Downloading Everything SDK from $SdkUrl..." -ForegroundColor Yellow
try {
    Invoke-WebRequest -Uri $SdkUrl -OutFile $TempZip -UseBasicParsing
    Write-Host "[OK] Download complete" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] Failed to download SDK: $_" -ForegroundColor Red
    exit 1
}

# Extract SDK
Write-Host "Extracting SDK..." -ForegroundColor Yellow
try {
    if (Test-Path $TempExtract) {
        Remove-Item -Recurse -Force $TempExtract
    }
    Expand-Archive -Path $TempZip -DestinationPath $TempExtract -Force
    Write-Host "[OK] Extraction complete" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] Failed to extract SDK: $_" -ForegroundColor Red
    exit 1
}

# Find and copy DLL files
Write-Host "Copying DLL files..." -ForegroundColor Yellow

$dll64 = Get-ChildItem -Path $TempExtract -Filter "Everything64.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
$dll32 = Get-ChildItem -Path $TempExtract -Filter "Everything.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

$copiedFiles = 0

if ($dll64) {
    Copy-Item -Path $dll64.FullName -Destination $TargetPath -Force
    Write-Host "  [OK] Copied Everything64.dll" -ForegroundColor Green
    $copiedFiles++
} else {
    Write-Host "  [WARN] Everything64.dll not found in SDK" -ForegroundColor Yellow
}

if ($dll32) {
    Copy-Item -Path $dll32.FullName -Destination $TargetPath -Force
    Write-Host "  [OK] Copied Everything.dll" -ForegroundColor Green
    $copiedFiles++
} else {
    Write-Host "  [WARN] Everything.dll not found in SDK" -ForegroundColor Yellow
}

# Copy header files if they exist
$headerFiles = Get-ChildItem -Path $TempExtract -Filter "*.h" -Recurse -ErrorAction SilentlyContinue
foreach ($header in $headerFiles) {
    Copy-Item -Path $header.FullName -Destination $TargetPath -Force
    Write-Host "  [OK] Copied $($header.Name)" -ForegroundColor Green
    $copiedFiles++
}

# Cleanup
Write-Host "Cleaning up temporary files..." -ForegroundColor Yellow
Remove-Item -Path $TempZip -Force -ErrorAction SilentlyContinue
Remove-Item -Path $TempExtract -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
if ($copiedFiles -gt 0) {
    Write-Host "=== SUCCESS ===" -ForegroundColor Green
    Write-Host "Everything SDK files copied to: $TargetPath" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "1. Install Everything from https://www.voidtools.com/" -ForegroundColor White
    Write-Host "2. Run Everything and ensure it is indexing your drives" -ForegroundColor White
    Write-Host "3. Build KAssetManager - the DLL will be copied to the output directory" -ForegroundColor White
    Write-Host "4. Launch KAssetManager and test the Everything search integration" -ForegroundColor White
} else {
    Write-Host "=== WARNING ===" -ForegroundColor Yellow
    Write-Host "No DLL files were found in the SDK archive." -ForegroundColor Yellow
    $sdkUrl = "https://www.voidtools.com/support/everything/sdk/"
    Write-Host "Please download manually from: $sdkUrl" -ForegroundColor Yellow
}

Write-Host ""

