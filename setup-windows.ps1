# KAsset Manager - Windows Setup Script
# This script automates the setup process for Windows 11

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "KAsset Manager - Windows Setup Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Function to check if a command exists
function Test-Command {
    param($Command)
    try {
        if (Get-Command $Command -ErrorAction Stop) {
            return $true
        }
    }
    catch {
        return $false
    }
}

# Function to check if a service is running
function Test-Service {
    param($ServiceName)
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    return ($service -and $service.Status -eq 'Running')
}

# Step 1: Check Prerequisites
Write-Host "Step 1: Checking Prerequisites..." -ForegroundColor Yellow
Write-Host ""

# Check Node.js
if (Test-Command "node") {
    $nodeVersion = node --version
    Write-Host "[OK] Node.js installed: $nodeVersion" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Node.js not found. Please install Node.js 20+ from https://nodejs.org/" -ForegroundColor Red
    exit 1
}

# Check PostgreSQL
if (Test-Command "psql") {
    $pgVersion = psql --version
    Write-Host "[OK] PostgreSQL installed: $pgVersion" -ForegroundColor Green
} else {
    Write-Host "[WARNING] PostgreSQL not found in PATH" -ForegroundColor Yellow
    Write-Host "Please install PostgreSQL from https://www.postgresql.org/download/windows/" -ForegroundColor Yellow
}

# Check Redis/Memurai
$redisRunning = $false
if (Test-Command "memurai-cli") {
    Write-Host "[OK] Memurai installed" -ForegroundColor Green
    $redisRunning = Test-Service "Memurai"
} elseif (Test-Command "redis-cli") {
    Write-Host "[OK] Redis installed" -ForegroundColor Green
} else {
    Write-Host "[WARNING] Redis/Memurai not found" -ForegroundColor Yellow
    Write-Host "Please install Memurai from https://www.memurai.com/get-memurai" -ForegroundColor Yellow
}

# Check FFMPEG
if (Test-Command "ffmpeg") {
    $ffmpegVersion = ffmpeg -version | Select-Object -First 1
    Write-Host "[OK] FFMPEG installed" -ForegroundColor Green
} else {
    Write-Host "[WARNING] FFMPEG not found in PATH" -ForegroundColor Yellow
    Write-Host "Please install FFMPEG from https://www.gyan.dev/ffmpeg/builds/" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Press any key to continue or Ctrl+C to exit..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")

# Step 2: Start Services
Write-Host ""
Write-Host "Step 2: Starting Services..." -ForegroundColor Yellow
Write-Host ""

# Start PostgreSQL
$pgService = Get-Service -Name "postgresql*" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pgService) {
    if ($pgService.Status -ne 'Running') {
        Write-Host "Starting PostgreSQL service..." -ForegroundColor Cyan
        Start-Service $pgService.Name
        Write-Host "[OK] PostgreSQL started" -ForegroundColor Green
    } else {
        Write-Host "[OK] PostgreSQL already running" -ForegroundColor Green
    }
} else {
    Write-Host "[WARNING] PostgreSQL service not found" -ForegroundColor Yellow
}

# Start Memurai/Redis
if (Test-Service "Memurai") {
    Write-Host "[OK] Memurai already running" -ForegroundColor Green
} else {
    $memuriService = Get-Service -Name "Memurai" -ErrorAction SilentlyContinue
    if ($memuriService) {
        Write-Host "Starting Memurai service..." -ForegroundColor Cyan
        Start-Service Memurai
        Write-Host "[OK] Memurai started" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] Memurai service not found. Please start Redis manually." -ForegroundColor Yellow
    }
}

# Step 3: Setup Database
Write-Host ""
Write-Host "Step 3: Setting up Database..." -ForegroundColor Yellow
Write-Host ""

Write-Host "Enter PostgreSQL password for user 'postgres':" -ForegroundColor Cyan
$pgPassword = Read-Host -AsSecureString
$pgPasswordPlain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($pgPassword))

# Check if database exists
$env:PGPASSWORD = $pgPasswordPlain
$dbExists = psql -U postgres -lqt 2>$null | Select-String -Pattern "kasset_manager"

if ($dbExists) {
    Write-Host "[OK] Database 'kasset_manager' already exists" -ForegroundColor Green
} else {
    Write-Host "Creating database 'kasset_manager'..." -ForegroundColor Cyan
    $createDb = "CREATE DATABASE kasset_manager;"
    $result = $createDb | psql -U postgres 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Database created successfully" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Failed to create database: $result" -ForegroundColor Red
        Write-Host "Please create the database manually using pgAdmin or psql" -ForegroundColor Yellow
    }
}

# Step 4: Install Dependencies
Write-Host ""
Write-Host "Step 4: Installing Dependencies..." -ForegroundColor Yellow
Write-Host ""

if (Test-Path "package.json") {
    Write-Host "Installing root dependencies..." -ForegroundColor Cyan
    npm install
    
    Write-Host "Installing backend dependencies..." -ForegroundColor Cyan
    Set-Location backend
    npm install
    Set-Location ..
    
    Write-Host "Installing frontend dependencies..." -ForegroundColor Cyan
    Set-Location frontend
    npm install
    Set-Location ..
    
    Write-Host "[OK] All dependencies installed" -ForegroundColor Green
} else {
    Write-Host "[ERROR] package.json not found. Are you in the correct directory?" -ForegroundColor Red
    exit 1
}

# Step 5: Configure Environment
Write-Host ""
Write-Host "Step 5: Configuring Environment..." -ForegroundColor Yellow
Write-Host ""

if (Test-Path "backend\.env") {
    Write-Host "[OK] .env file already exists" -ForegroundColor Green
} else {
    if (Test-Path "backend\.env.example") {
        Write-Host "Creating .env file from template..." -ForegroundColor Cyan
        Copy-Item "backend\.env.example" "backend\.env"
        
        # Update .env with user's password
        $envContent = Get-Content "backend\.env"
        $envContent = $envContent -replace "DB_PASSWORD=.*", "DB_PASSWORD=$pgPasswordPlain"
        $envContent | Set-Content "backend\.env"
        
        Write-Host "[OK] .env file created" -ForegroundColor Green
        Write-Host "[INFO] Please review and update backend\.env if needed" -ForegroundColor Cyan
    } else {
        Write-Host "[ERROR] .env.example not found" -ForegroundColor Red
    }
}

# Step 6: Run Migrations
Write-Host ""
Write-Host "Step 6: Running Database Migrations..." -ForegroundColor Yellow
Write-Host ""

Write-Host "Running migrations..." -ForegroundColor Cyan
Set-Location backend
$migrationResult = npm run migration:run 2>&1
Set-Location ..

if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] Migrations completed successfully" -ForegroundColor Green
} else {
    Write-Host "[WARNING] Migrations may have failed. Check the output above." -ForegroundColor Yellow
}

# Step 7: Build Projects
Write-Host ""
Write-Host "Step 7: Building Projects..." -ForegroundColor Yellow
Write-Host ""

Write-Host "Building backend..." -ForegroundColor Cyan
Set-Location backend
npm run build
Set-Location ..

Write-Host "Building frontend..." -ForegroundColor Cyan
Set-Location frontend
npm run build
Set-Location ..

Write-Host "[OK] Build completed" -ForegroundColor Green

# Step 8: Final Checks
Write-Host ""
Write-Host "Step 8: Running Final Checks..." -ForegroundColor Yellow
Write-Host ""

# Test Redis connection
if (Test-Command "memurai-cli") {
    $redisPing = memurai-cli ping 2>$null
    if ($redisPing -eq "PONG") {
        Write-Host "[OK] Redis/Memurai connection successful" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] Cannot connect to Redis/Memurai" -ForegroundColor Yellow
    }
}

# Test PostgreSQL connection
$testQuery = "SELECT 1;"
$testResult = $testQuery | psql -U postgres -d kasset_manager 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] PostgreSQL connection successful" -ForegroundColor Green
} else {
    Write-Host "[WARNING] Cannot connect to PostgreSQL" -ForegroundColor Yellow
}

# Clear password from environment
$env:PGPASSWORD = ""

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setup Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "To start the application:" -ForegroundColor Cyan
Write-Host "  npm run dev:all" -ForegroundColor White
Write-Host ""
Write-Host "Backend will be available at: http://localhost:3000" -ForegroundColor Cyan
Write-Host "Frontend will be available at: http://localhost:5173" -ForegroundColor Cyan
Write-Host ""
Write-Host "To test the backend health:" -ForegroundColor Cyan
Write-Host "  curl http://localhost:3000/health" -ForegroundColor White
Write-Host ""
Write-Host "For more information, see SETUP-WINDOWS.md" -ForegroundColor Cyan
Write-Host ""

