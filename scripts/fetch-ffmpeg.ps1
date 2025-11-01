param(
    [string]$DownloadUrl = "",
    [switch]$Git
)

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = "Stop"

if (-not $DownloadUrl) {
    if ($Git.IsPresent) {
        $DownloadUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-full.7z"
    } else {
        # Default to the latest release full shared build (MSVC, DLLs + import libs)
        $DownloadUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z"
    }
}

$repoRoot = (Resolve-Path "$PSScriptRoot/.." ).Path
$destRoot = Join-Path $repoRoot "third_party"
if (-not (Test-Path $destRoot)) {
    New-Item -Path $destRoot -ItemType Directory | Out-Null
}

$ffmpegDir = Join-Path $destRoot "ffmpeg"
if (Test-Path $ffmpegDir) {
    Write-Host "Removing existing third_party/ffmpeg directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $ffmpegDir
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("ffmpeg-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -Path $tempDir -ItemType Directory | Out-Null

$fileName = Split-Path -Path $DownloadUrl -Leaf
$archivePath = Join-Path $tempDir $fileName

Write-Host "Downloading FFmpeg build from $DownloadUrl" -ForegroundColor Cyan
Invoke-WebRequest -Uri $DownloadUrl -OutFile $archivePath

Write-Host "Extracting archive..." -ForegroundColor Cyan

$extension = [System.IO.Path]::GetExtension($archivePath)
switch ($extension.ToLowerInvariant()) {
    ".zip" {
        Expand-Archive -Path $archivePath -DestinationPath $tempDir
    }
    ".7z" {
        $tar = Get-Command tar -ErrorAction SilentlyContinue
        if (-not $tar) {
            throw "Cannot extract .7z archives because 'tar' is not available in PATH. Install 7-Zip or ensure tar.exe is accessible."
        }
        & $tar.Path -xf $archivePath -C $tempDir
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to extract $fileName (tar exit code $LASTEXITCODE)."
        }
    }
    default {
        throw "Unsupported archive format: $extension"
    }
}

$extracted = Get-ChildItem -Path $tempDir -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $extracted) {
    throw "Failed to locate extracted FFmpeg directory."
}

Move-Item -Path $extracted.FullName -Destination $ffmpegDir

Write-Host "FFmpeg files placed in $ffmpegDir" -ForegroundColor Green

Remove-Item -Recurse -Force $tempDir

Write-Host "Fetch complete." -ForegroundColor Green
