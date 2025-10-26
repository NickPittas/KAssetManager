# Convert PNG to ICO format for Windows installer and application
# This script converts icon.png to icon.ico with multiple sizes

param(
    [string]$InputPng = "icon.png",
    [string]$OutputIco = "icon.ico"
)

$ErrorActionPreference = "Stop"

Write-Host "Converting $InputPng to $OutputIco..." -ForegroundColor Cyan

# Check if input file exists
if (-not (Test-Path $InputPng)) {
    Write-Error "Input file not found: $InputPng"
    exit 1
}

# Load System.Drawing assembly
Add-Type -AssemblyName System.Drawing

# Load the PNG image
$png = [System.Drawing.Image]::FromFile((Resolve-Path $InputPng).Path)

# Create icon with multiple sizes (16x16, 32x32, 48x48, 64x64, 128x128, 256x256)
$sizes = @(16, 32, 48, 64, 128, 256)
$memoryStream = New-Object System.IO.MemoryStream

# ICO file header
$iconDir = [byte[]]::new(6)
$iconDir[0] = 0  # Reserved
$iconDir[1] = 0  # Reserved
$iconDir[2] = 1  # Type: 1 = ICO
$iconDir[3] = 0  # Type (continued)
$iconDir[4] = [byte]$sizes.Count  # Number of images
$iconDir[5] = 0  # Number of images (high byte)

$memoryStream.Write($iconDir, 0, 6)

# Prepare image data
$imageDataList = @()
$offset = 6 + ($sizes.Count * 16)  # Header + directory entries

foreach ($size in $sizes) {
    # Create bitmap at this size
    $bitmap = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($png, 0, 0, $size, $size)
    $graphics.Dispose()
    
    # Convert to PNG format in memory
    $imageStream = New-Object System.IO.MemoryStream
    $bitmap.Save($imageStream, [System.Drawing.Imaging.ImageFormat]::Png)
    $imageData = $imageStream.ToArray()
    $imageStream.Dispose()
    $bitmap.Dispose()
    
    # Create directory entry
    $entry = [byte[]]::new(16)
    $widthByte = if ($size -eq 256) { 0 } else { $size }
    $heightByte = if ($size -eq 256) { 0 } else { $size }
    $entry[0] = [byte]$widthByte  # Width (0 means 256)
    $entry[1] = [byte]$heightByte  # Height (0 means 256)
    $entry[2] = 0  # Color palette
    $entry[3] = 0  # Reserved
    $entry[4] = 1  # Color planes
    $entry[5] = 0  # Color planes (high byte)
    $entry[6] = 32  # Bits per pixel
    $entry[7] = 0   # Bits per pixel (high byte)
    
    # Image size (4 bytes, little-endian)
    $sizeBytes = [BitConverter]::GetBytes([int]$imageData.Length)
    $entry[8] = $sizeBytes[0]
    $entry[9] = $sizeBytes[1]
    $entry[10] = $sizeBytes[2]
    $entry[11] = $sizeBytes[3]
    
    # Image offset (4 bytes, little-endian)
    $offsetBytes = [BitConverter]::GetBytes([int]$offset)
    $entry[12] = $offsetBytes[0]
    $entry[13] = $offsetBytes[1]
    $entry[14] = $offsetBytes[2]
    $entry[15] = $offsetBytes[3]
    
    $memoryStream.Write($entry, 0, 16)
    $imageDataList += $imageData
    $offset += $imageData.Length
}

# Write all image data
foreach ($imageData in $imageDataList) {
    $memoryStream.Write($imageData, 0, $imageData.Length)
}

# Save to file
$icoBytes = $memoryStream.ToArray()
[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $OutputIco), $icoBytes)

$memoryStream.Dispose()
$png.Dispose()

Write-Host "Successfully created $OutputIco with $($sizes.Count) sizes: $($sizes -join ', ')" -ForegroundColor Green

