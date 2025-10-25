# KAsset Manager - Test Setup Script
# This script starts portable PostgreSQL and Redis for testing

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "KAsset Manager - Test Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = "Stop"
$rootDir = Join-Path $PSScriptRoot ".."
$resourcesDir = Join-Path $rootDir "resources"
$dataDir = Join-Path $rootDir "test-data"

# Create data directory
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
$pgDataDir = Join-Path $dataDir "postgresql"
$redisDataDir = Join-Path $dataDir "redis"
New-Item -ItemType Directory -Force -Path $pgDataDir | Out-Null
New-Item -ItemType Directory -Force -Path $redisDataDir | Out-Null

# PostgreSQL paths
$pgDir = Join-Path $resourcesDir "postgresql"
$pgBinDir = Join-Path $pgDir "bin"
$pgExe = Join-Path $pgBinDir "postgres.exe"
$initdbExe = Join-Path $pgBinDir "initdb.exe"
$psqlExe = Join-Path $pgBinDir "psql.exe"

# Redis paths
$redisDir = Join-Path $resourcesDir "redis"
$redisExe = Join-Path $redisDir "redis-server.exe"

# Check if dependencies exist
if (!(Test-Path $pgExe)) {
    Write-Host "[ERROR] PostgreSQL not found. Run: npm run prepare:deps" -ForegroundColor Red
    exit 1
}

if (!(Test-Path $redisExe)) {
    Write-Host "[ERROR] Redis not found. Run: npm run prepare:deps" -ForegroundColor Red
    exit 1
}

# Initialize PostgreSQL if needed
if (!(Test-Path (Join-Path $pgDataDir "PG_VERSION"))) {
    Write-Host "Initializing PostgreSQL database..." -ForegroundColor Yellow
    & $initdbExe -D $pgDataDir -U postgres -A trust --locale=C --encoding=UTF8
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to initialize PostgreSQL" -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] PostgreSQL initialized" -ForegroundColor Green
}

# Start PostgreSQL
Write-Host ""
Write-Host "Starting PostgreSQL on port 54320..." -ForegroundColor Yellow
$pgProcess = Start-Process -FilePath $pgExe -ArgumentList "-D", $pgDataDir, "-p", "54320" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3

if ($pgProcess.HasExited) {
    Write-Host "[ERROR] PostgreSQL failed to start" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] PostgreSQL started (PID: $($pgProcess.Id))" -ForegroundColor Green

# Create database and user
Write-Host ""
Write-Host "Creating database and user..." -ForegroundColor Yellow
Start-Sleep -Seconds 2

$env:PGPASSWORD = ""
$ErrorActionPreference = "Continue"
& $psqlExe -h localhost -p 54320 -U postgres -c "CREATE USER kasset WITH PASSWORD 'kasset_dev';" 2>&1 | Out-Null
& $psqlExe -h localhost -p 54320 -U postgres -c "CREATE DATABASE kasset_manager OWNER kasset;" 2>&1 | Out-Null
& $psqlExe -h localhost -p 54320 -U postgres -c "GRANT ALL PRIVILEGES ON DATABASE kasset_manager TO kasset;" 2>&1 | Out-Null
$ErrorActionPreference = "Stop"

Write-Host "[OK] Database ready" -ForegroundColor Green

# Start Redis
Write-Host ""
Write-Host "Starting Redis on port 63790..." -ForegroundColor Yellow
$redisProcess = Start-Process -FilePath $redisExe -ArgumentList "--port", "63790", "--bind", "127.0.0.1", "--maxmemory", "256mb", "--maxmemory-policy", "allkeys-lru", "--dir", $redisDataDir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

if ($redisProcess.HasExited) {
    Write-Host "[ERROR] Redis failed to start" -ForegroundColor Red
    Stop-Process -Id $pgProcess.Id -Force
    exit 1
}
Write-Host "[OK] Redis started (PID: $($redisProcess.Id))" -ForegroundColor Green

# Create backend .env file
Write-Host ""
Write-Host "Creating backend .env file..." -ForegroundColor Yellow
$backendDir = Join-Path $rootDir "backend"
$envFile = Join-Path $backendDir ".env"

$envContent = @"
# Database
DATABASE_HOST=localhost
DATABASE_PORT=54320
DATABASE_USER=kasset
DATABASE_PASSWORD=kasset_dev
DATABASE_NAME=kasset_manager

# Redis
REDIS_HOST=localhost
REDIS_PORT=63790

# Application
PORT=3000
NODE_ENV=development

# Cache
CACHE_PATH=$($dataDir)\cache
CACHE_QUOTA_GB=10

# LLM
OLLAMA_URL=http://localhost:11434
LM_STUDIO_URL=http://localhost:1234

# FFMPEG
FFMPEG_PATH=$($resourcesDir)\ffmpeg\ffmpeg.exe
FFPROBE_PATH=$($resourcesDir)\ffmpeg\ffprobe.exe
"@

$envContent | Out-File -FilePath $envFile -Encoding UTF8 -Force
Write-Host "[OK] .env file created" -ForegroundColor Green

# Save process IDs for cleanup
$pidsFile = Join-Path $dataDir "pids.txt"
"$($pgProcess.Id),$($redisProcess.Id)" | Out-File -FilePath $pidsFile -Encoding UTF8

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Test environment ready!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Services running:" -ForegroundColor Cyan
Write-Host "  PostgreSQL: localhost:54320 (PID: $($pgProcess.Id))" -ForegroundColor White
Write-Host "  Redis: localhost:63790 (PID: $($redisProcess.Id))" -ForegroundColor White
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Run migrations: cd backend && npm run migration:run" -ForegroundColor White
Write-Host "  2. Start backend: cd backend && npm run start:dev" -ForegroundColor White
Write-Host "  3. Start frontend: cd frontend && npm run dev" -ForegroundColor White
Write-Host ""
Write-Host "To stop services, run: .\scripts\test-cleanup.ps1" -ForegroundColor Yellow
Write-Host ""

