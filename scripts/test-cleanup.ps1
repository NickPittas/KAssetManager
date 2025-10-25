# KAsset Manager - Test Cleanup Script
# This script stops the test PostgreSQL and Redis services

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "KAsset Manager - Test Cleanup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = "Continue"
$rootDir = Join-Path $PSScriptRoot ".."
$dataDir = Join-Path $rootDir "test-data"
$pidsFile = Join-Path $dataDir "pids.txt"

if (Test-Path $pidsFile) {
    $pids = Get-Content $pidsFile
    $pidArray = $pids.Split(',')
    
    foreach ($pid in $pidArray) {
        if ($pid -match '^\d+$') {
            try {
                $process = Get-Process -Id $pid -ErrorAction SilentlyContinue
                if ($process) {
                    Write-Host "Stopping process $pid ($($process.ProcessName))..." -ForegroundColor Yellow
                    Stop-Process -Id $pid -Force
                    Write-Host "[OK] Process stopped" -ForegroundColor Green
                }
            } catch {
                Write-Host "[WARN] Process $pid not found" -ForegroundColor Yellow
            }
        }
    }
    
    Remove-Item $pidsFile -Force
} else {
    Write-Host "[WARN] No PIDs file found" -ForegroundColor Yellow
}

# Also try to stop by process name
Write-Host ""
Write-Host "Checking for any remaining processes..." -ForegroundColor Yellow

$postgresProcesses = Get-Process -Name postgres -ErrorAction SilentlyContinue
if ($postgresProcesses) {
    foreach ($proc in $postgresProcesses) {
        Write-Host "Stopping PostgreSQL process $($proc.Id)..." -ForegroundColor Yellow
        Stop-Process -Id $proc.Id -Force
    }
    Write-Host "[OK] PostgreSQL processes stopped" -ForegroundColor Green
}

$redisProcesses = Get-Process -Name redis-server -ErrorAction SilentlyContinue
if ($redisProcesses) {
    foreach ($proc in $redisProcesses) {
        Write-Host "Stopping Redis process $($proc.Id)..." -ForegroundColor Yellow
        Stop-Process -Id $proc.Id -Force
    }
    Write-Host "[OK] Redis processes stopped" -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Cleanup complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

