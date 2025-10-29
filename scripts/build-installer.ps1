# Build KAsset Manager Installer
# This script builds the NSIS installer for KAsset Manager

param(
    [string]$NSISPath = "C:\Program Files (x86)\NSIS\makensis.exe",
    [switch]$SkipBuild = $false
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "KAsset Manager Installer Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Get script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$InstallerDir = Join-Path $RootDir "installer"
$DistDir = Join-Path $RootDir "dist"
$PortableDir = Join-Path $DistDir "portable"

# Check if NSIS is installed
if (-not (Test-Path $NSISPath)) {
    Write-Host "ERROR: NSIS not found at: $NSISPath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install NSIS from: https://nsis.sourceforge.io/" -ForegroundColor Yellow
    Write-Host "Or specify the correct path with -NSISPath parameter" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Example:" -ForegroundColor Cyan
    Write-Host '  .\scripts\build-installer.ps1 -NSISPath "C:\Program Files (x86)\NSIS\makensis.exe"' -ForegroundColor Cyan
    exit 1
}

Write-Host "NSIS found: $NSISPath" -ForegroundColor Green
Write-Host ""

# Build the application first (unless skipped)
if (-not $SkipBuild) {
    Write-Host "Building application..." -ForegroundColor Cyan
    $BuildScript = Join-Path $ScriptDir "build-windows.ps1"
    
    if (-not (Test-Path $BuildScript)) {
        Write-Host "ERROR: Build script not found: $BuildScript" -ForegroundColor Red
        exit 1
    }
    
    # Run build script with Package flag
    & powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -Generator "Visual Studio 17 2022" -Package
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Build failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "Skipping build (using existing portable distribution)" -ForegroundColor Yellow
    Write-Host ""
}

# Check if portable distribution exists
if (-not (Test-Path $PortableDir)) {
    Write-Host "ERROR: Portable distribution not found at: $PortableDir" -ForegroundColor Red
    Write-Host "Please run the build script first without -SkipBuild" -ForegroundColor Yellow
    exit 1
}

Write-Host "Portable distribution found: $PortableDir" -ForegroundColor Green
Write-Host ""

# Check if LICENSE.txt exists, create a placeholder if not
$LicensePath = Join-Path $RootDir "LICENSE.txt"
if (-not (Test-Path $LicensePath)) {
    Write-Host "WARNING: LICENSE.txt not found, creating placeholder..." -ForegroundColor Yellow
    @"
KAsset Manager License

Copyright (C) 2024 Your Company Name

All rights reserved.

This software is proprietary and confidential.
Unauthorized copying, distribution, or use is strictly prohibited.
"@ | Out-File -FilePath $LicensePath -Encoding UTF8
    Write-Host "Created placeholder LICENSE.txt" -ForegroundColor Green
    Write-Host ""
}

# Create installer directory if it doesn't exist
if (-not (Test-Path $InstallerDir)) {
    New-Item -ItemType Directory -Path $InstallerDir | Out-Null
    Write-Host "Created installer directory: $InstallerDir" -ForegroundColor Green
}

# Check if NSIS script exists
$NSIScript = Join-Path $InstallerDir "kassetmanager.nsi"
if (-not (Test-Path $NSIScript)) {
    Write-Host "ERROR: NSIS script not found: $NSIScript" -ForegroundColor Red
    exit 1
}

Write-Host "NSIS script found: $NSIScript" -ForegroundColor Green
Write-Host ""

# Build the installer
Write-Host "Building installer..." -ForegroundColor Cyan
Write-Host ""

# Change to root directory so relative paths in NSI script work
Push-Location $RootDir

try {
    # Run NSIS compiler
    & $NSISPath $NSIScript
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Installer build failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Installer built successfully!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    # Find the installer file
    $InstallerFile = Get-ChildItem -Path $DistDir -Filter "KAssetManager-Setup-*.exe" | Select-Object -First 1
    
    if ($InstallerFile) {
        Write-Host "Installer location:" -ForegroundColor Cyan
        Write-Host "  $($InstallerFile.FullName)" -ForegroundColor White
        Write-Host ""
        
        # Get file size
        $SizeMB = [math]::Round($InstallerFile.Length / 1MB, 2)
        Write-Host "Installer size: $SizeMB MB" -ForegroundColor Cyan
        Write-Host ""
        
        # Calculate SHA256 hash
        Write-Host "Calculating SHA256 hash..." -ForegroundColor Cyan
        $Hash = Get-FileHash -Path $InstallerFile.FullName -Algorithm SHA256
        Write-Host "SHA256: $($Hash.Hash)" -ForegroundColor White
        Write-Host ""
        
        # Save hash to file
        $HashFile = Join-Path $DistDir "KAssetManager-Setup-0.2.0.exe.sha256"
        $Hash.Hash | Out-File -FilePath $HashFile -Encoding ASCII
        Write-Host "Hash saved to: $HashFile" -ForegroundColor Green
        Write-Host ""
        
        Write-Host "You can now distribute the installer!" -ForegroundColor Green
        Write-Host ""
        Write-Host "To test the installer:" -ForegroundColor Yellow
        Write-Host "  1. Run the installer as Administrator" -ForegroundColor White
        Write-Host "  2. Follow the installation wizard" -ForegroundColor White
        Write-Host "  3. Launch KAsset Manager from Start Menu or Desktop" -ForegroundColor White
        Write-Host ""
        
    } else {
        Write-Host "WARNING: Installer file not found in dist directory" -ForegroundColor Yellow
    }
    
} finally {
    Pop-Location
}

Write-Host "Done!" -ForegroundColor Green

