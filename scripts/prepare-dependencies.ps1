# KAsset Manager - Prepare Portable Dependencies
# This script downloads and prepares all portable dependencies for bundling

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Preparing Portable Dependencies" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = "Stop"
$resourcesDir = Join-Path (Join-Path $PSScriptRoot "..") "resources"

# Create resources directory
New-Item -ItemType Directory -Force -Path $resourcesDir | Out-Null

# PostgreSQL Portable
Write-Host "1. Downloading PostgreSQL Portable..." -ForegroundColor Yellow
$pgVersion = "16.1-1"
$pgUrl = "https://get.enterprisedb.com/postgresql/postgresql-$pgVersion-windows-x64-binaries.zip"
$pgZip = Join-Path $resourcesDir "postgresql.zip"
$pgDir = Join-Path $resourcesDir "postgresql"

if (!(Test-Path $pgDir)) {
    Write-Host "   Downloading from $pgUrl..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $pgUrl -OutFile $pgZip -UseBasicParsing
    
    Write-Host "   Extracting PostgreSQL..." -ForegroundColor Cyan
    Expand-Archive -Path $pgZip -DestinationPath $resourcesDir -Force
    
    # Rename to postgresql
    $extractedDir = Join-Path $resourcesDir "pgsql"
    if (Test-Path $extractedDir) {
        Rename-Item -Path $extractedDir -NewName "postgresql"
    }
    
    Remove-Item $pgZip -Force
    Write-Host "   [OK] PostgreSQL prepared" -ForegroundColor Green
} else {
    Write-Host "   [OK] PostgreSQL already exists" -ForegroundColor Green
}

# Redis for Windows (Memurai)
Write-Host ""
Write-Host "2. Downloading Redis for Windows..." -ForegroundColor Yellow
$redisVersion = "7.2.4"
$redisUrl = "https://github.com/redis-windows/redis-windows/releases/download/$redisVersion/Redis-$redisVersion-Windows-x64-msys2.zip"
$redisZip = Join-Path $resourcesDir "redis.zip"
$redisDir = Join-Path $resourcesDir "redis"

if (!(Test-Path $redisDir)) {
    Write-Host "   Downloading from $redisUrl..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $redisUrl -OutFile $redisZip -UseBasicParsing

    Write-Host "   Extracting Redis..." -ForegroundColor Cyan
    $tempExtractDir = Join-Path $resourcesDir "redis_temp"
    Expand-Archive -Path $redisZip -DestinationPath $tempExtractDir -Force

    # Find the extracted directory and move contents
    $extractedSubDir = Get-ChildItem -Path $tempExtractDir -Directory | Select-Object -First 1
    if ($extractedSubDir) {
        New-Item -ItemType Directory -Force -Path $redisDir | Out-Null
        Get-ChildItem -Path $extractedSubDir.FullName | Move-Item -Destination $redisDir -Force
        Remove-Item $tempExtractDir -Recurse -Force
    }

    Remove-Item $redisZip -Force

    # Create redis.conf
    $redisConf = @"
port 63790
bind 127.0.0.1
protected-mode yes
daemonize no
supervised no
pidfile redis.pid
loglevel notice
logfile ""
databases 16
save 900 1
save 300 10
save 60 10000
stop-writes-on-bgsave-error yes
rdbcompression yes
rdbchecksum yes
dbfilename dump.rdb
dir ./
maxmemory 256mb
maxmemory-policy allkeys-lru
"@
    
    $redisConf | Out-File -FilePath (Join-Path $redisDir "redis.conf") -Encoding UTF8
    
    Write-Host "   [OK] Redis prepared" -ForegroundColor Green
} else {
    Write-Host "   [OK] Redis already exists" -ForegroundColor Green
}

# FFMPEG
Write-Host ""
Write-Host "3. Downloading FFMPEG..." -ForegroundColor Yellow
$ffmpegVersion = "6.1"
$ffmpegUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
$ffmpegZip = Join-Path $resourcesDir "ffmpeg.zip"
$ffmpegDir = Join-Path $resourcesDir "ffmpeg"

if (!(Test-Path $ffmpegDir)) {
    Write-Host "   Downloading from $ffmpegUrl..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $ffmpegUrl -OutFile $ffmpegZip -UseBasicParsing
    
    Write-Host "   Extracting FFMPEG..." -ForegroundColor Cyan
    Expand-Archive -Path $ffmpegZip -DestinationPath $resourcesDir -Force
    
    # Find the extracted directory (it has a version number)
    $extractedDir = Get-ChildItem -Path $resourcesDir -Directory | Where-Object { $_.Name -like "ffmpeg-*" } | Select-Object -First 1
    
    if ($extractedDir) {
        # Move bin contents to ffmpeg directory
        New-Item -ItemType Directory -Force -Path $ffmpegDir | Out-Null
        $binDir = Join-Path $extractedDir.FullName "bin"
        Get-ChildItem -Path $binDir | Move-Item -Destination $ffmpegDir -Force
        
        # Remove extracted directory
        Remove-Item $extractedDir.FullName -Recurse -Force
    }
    
    Remove-Item $ffmpegZip -Force
    Write-Host "   [OK] FFMPEG prepared" -ForegroundColor Green
} else {
    Write-Host "   [OK] FFMPEG already exists" -ForegroundColor Green
}

# Verify all dependencies
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Verification" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$allGood = $true

# Check PostgreSQL
$pgBinDir = Join-Path $pgDir "bin"
$pgExe = Join-Path $pgBinDir "postgres.exe"
if (Test-Path $pgExe) {
    $pgSize = (Get-Item $pgExe).Length / 1MB
    Write-Host "[OK] PostgreSQL: $([math]::Round($pgSize, 2)) MB" -ForegroundColor Green
} else {
    Write-Host "[ERROR] PostgreSQL not found" -ForegroundColor Red
    $allGood = $false
}

# Check Redis
$redisExe = Join-Path $redisDir "redis-server.exe"
if (Test-Path $redisExe) {
    $redisSize = (Get-Item $redisExe).Length / 1MB
    Write-Host "[OK] Redis: $([math]::Round($redisSize, 2)) MB" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Redis not found" -ForegroundColor Red
    $allGood = $false
}

# Check FFMPEG
$ffmpegExe = Join-Path $ffmpegDir "ffmpeg.exe"
if (Test-Path $ffmpegExe) {
    $ffmpegSize = (Get-Item $ffmpegExe).Length / 1MB
    Write-Host "[OK] FFMPEG: $([math]::Round($ffmpegSize, 2)) MB" -ForegroundColor Green
} else {
    Write-Host "[ERROR] FFMPEG not found" -ForegroundColor Red
    $allGood = $false
}

# Calculate total size
$totalSize = 0
if (Test-Path $pgDir) {
    $totalSize += (Get-ChildItem -Path $pgDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
}
if (Test-Path $redisDir) {
    $totalSize += (Get-ChildItem -Path $redisDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
}
if (Test-Path $ffmpegDir) {
    $totalSize += (Get-ChildItem -Path $ffmpegDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
}

Write-Host ""
Write-Host "Total dependencies size: $([math]::Round($totalSize, 2)) MB" -ForegroundColor Cyan
Write-Host ""

if ($allGood) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "All dependencies prepared successfully!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "1. Build the application: npm run build:all" -ForegroundColor White
    Write-Host "2. Create installer: npm run dist" -ForegroundColor White
    Write-Host ""
} else {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Some dependencies failed to prepare" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    exit 1
}

