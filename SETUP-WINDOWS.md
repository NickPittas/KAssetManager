# KAsset Manager - Windows 11 Setup Guide

This guide will walk you through setting up KAsset Manager on Windows 11 from scratch.

## 📋 Prerequisites Installation

### 1. Install Node.js 20+

1. Download Node.js from: https://nodejs.org/
2. Run the installer (choose LTS version)
3. Verify installation:
```powershell
node --version
npm --version
```

### 2. Install PostgreSQL 14+

1. Download PostgreSQL from: https://www.postgresql.org/download/windows/
2. Run the installer
3. During installation:
   - Set password for postgres user (remember this!)
   - Port: 5432 (default)
   - Locale: Default
4. Add PostgreSQL to PATH:
   - The installer usually does this automatically
   - Verify: `psql --version`

**Alternative: Using Chocolatey**
```powershell
choco install postgresql14
```

### 3. Install Redis (Memurai for Windows)

**Option A: Memurai (Recommended for Windows)**

1. Download Memurai from: https://www.memurai.com/get-memurai
2. Run the installer
3. Memurai will run as a Windows Service automatically
4. Verify:
```powershell
memurai-cli ping
# Should return: PONG
```

**Option B: Redis for Windows (Legacy)**

1. Download from: https://github.com/microsoftarchive/redis/releases
2. Extract to `C:\Redis`
3. Run `redis-server.exe`

**Option C: Docker (If you have Docker Desktop)**

```powershell
docker run -d -p 6379:6379 --name redis redis:7-alpine
```

### 4. Install FFMPEG

**Option A: Using Chocolatey (Recommended)**
```powershell
choco install ffmpeg
```

**Option B: Manual Installation**

1. Download from: https://www.gyan.dev/ffmpeg/builds/
2. Extract to `C:\ffmpeg`
3. Add to PATH:
   - Open System Properties → Environment Variables
   - Edit PATH variable
   - Add `C:\ffmpeg\bin`
4. Verify:
```powershell
ffmpeg -version
ffprobe -version
```

## 🚀 Application Setup

### Step 1: Clone and Install Dependencies

```powershell
# Clone the repository
cd E:\
git clone <repository-url> KAssetManager
cd KAssetManager

# Install all dependencies (backend + frontend)
npm run install:all
```

### Step 2: Setup PostgreSQL Database

```powershell
# Open PowerShell as Administrator

# Create database using psql
psql -U postgres
# Enter your postgres password when prompted

# In psql prompt:
CREATE DATABASE kasset_manager;
\l
# You should see kasset_manager in the list
\q
```

**Alternative: Using pgAdmin**
1. Open pgAdmin (installed with PostgreSQL)
2. Connect to PostgreSQL server
3. Right-click "Databases" → Create → Database
4. Name: `kasset_manager`
5. Click Save

### Step 3: Verify Redis is Running

```powershell
# If using Memurai
memurai-cli ping
# Should return: PONG

# If using Redis
redis-cli ping
# Should return: PONG

# Check if Redis service is running
Get-Service | Where-Object {$_.Name -like "*redis*" -or $_.Name -like "*memurai*"}
```

**If Redis/Memurai is not running:**
```powershell
# Start Memurai service
Start-Service Memurai

# Or start Redis manually
cd C:\Redis
.\redis-server.exe
```

### Step 4: Configure Environment Variables

```powershell
cd backend

# Copy environment template
Copy-Item .env.example .env

# Edit .env file
notepad .env
```

**Update these values in .env:**

```env
# Database Configuration
DB_HOST=localhost
DB_PORT=5432
DB_NAME=kasset_manager
DB_USER=postgres
DB_PASSWORD=YOUR_POSTGRES_PASSWORD_HERE

# Redis Configuration
REDIS_HOST=localhost
REDIS_PORT=6379

# Application Configuration
PORT=3000
NODE_ENV=development

# Cache Configuration (Windows path)
CACHE_DIR=C:\Users\YourUsername\KAssets\cache
CACHE_QUOTA_GB=10

# LLM Configuration (Optional - only if you have Ollama installed)
LLM_ENDPOINT=http://localhost:11434
LLM_MODEL=qwen2-vl:7b
LLM_ENABLED=false

# Telemetry Configuration
TELEMETRY_ENABLED=true

# Backup Configuration
BACKUP_DIR=./backups

# CORS Configuration
CORS_ORIGIN=http://localhost:5173
```

### Step 5: Run Database Migrations

```powershell
# From the root directory
npm run migrate
```

**Expected output:**
```
Migration "CreateInitialSchema1234567890" has been executed successfully.
```

### Step 6: Start the Application

**Option 1: Start Everything (Recommended)**
```powershell
npm run dev:all
```

This will start:
- Backend API on http://localhost:3000
- Frontend dev server on http://localhost:5173

**Option 2: Start Individually**

Terminal 1 (Backend):
```powershell
npm run dev:backend
```

Terminal 2 (Frontend):
```powershell
npm run dev:frontend
```

### Step 7: Verify Everything is Working

**Test Backend:**
```powershell
# In a new PowerShell window
curl http://localhost:3000/health
# Should return: {"status":"ok"}
```

**Test Frontend:**
Open browser to: http://localhost:5173

You should see the KAsset Manager interface.

## 🧪 Testing the Application

```powershell
# Run all tests
npm test

# Run backend tests only
npm run test:backend

# Run frontend tests only
npm run test:frontend

# Run e2e tests
npm run test:e2e
```

## 🛠️ Troubleshooting

### Issue: "psql is not recognized"

**Solution:**
Add PostgreSQL to PATH:
```powershell
$env:Path += ";C:\Program Files\PostgreSQL\14\bin"
```

Or permanently:
1. System Properties → Environment Variables
2. Edit PATH
3. Add `C:\Program Files\PostgreSQL\14\bin`

### Issue: "Cannot connect to PostgreSQL"

**Check if PostgreSQL is running:**
```powershell
Get-Service postgresql*
```

**Start PostgreSQL service:**
```powershell
Start-Service postgresql-x64-14
```

**Test connection:**
```powershell
psql -U postgres -d kasset_manager -c "SELECT 1"
```

### Issue: "Cannot connect to Redis" (ECONNREFUSED 127.0.0.1:6379)

**Check if Memurai/Redis is running:**
```powershell
Get-Service | Where-Object {$_.Name -like "*memurai*"}
```

**Start Memurai:**
```powershell
Start-Service Memurai
```

**Test connection:**
```powershell
memurai-cli ping
# or
redis-cli ping
```

### Issue: "Port 3000 already in use"

**Find and kill process:**
```powershell
# Find process using port 3000
netstat -ano | findstr :3000

# Kill process (replace PID with actual process ID)
taskkill /PID <PID> /F

# Or use npm package
npx kill-port 3000
```

### Issue: "FFMPEG not found"

**Verify FFMPEG is in PATH:**
```powershell
where.exe ffmpeg
```

**If not found, add to PATH:**
```powershell
$env:Path += ";C:\ffmpeg\bin"
```

### Issue: Database migration fails

**Reset database:**
```powershell
# Drop and recreate database
psql -U postgres
DROP DATABASE kasset_manager;
CREATE DATABASE kasset_manager;
\q

# Run migrations again
npm run migrate
```

## 📦 Building for Production

```powershell
# Build everything
npm run build:all

# Start production servers
npm run start:backend  # Backend on port 3000
npm run start:frontend # Frontend preview
```

## 🎁 Creating Windows Installer (NOT YET IMPLEMENTED)

**Status:** ⚠️ Installer creation is planned but not yet implemented.

**What you would need:**
1. Electron Builder configured
2. FFMPEG binaries bundled
3. PostgreSQL portable version bundled
4. Memurai/Redis bundled
5. NSIS or WiX installer scripts

**Planned command:**
```powershell
npm run build:installer
```

## 📊 Monitoring Services

### Check all required services:
```powershell
# PostgreSQL
Get-Service postgresql*

# Redis/Memurai
Get-Service | Where-Object {$_.Name -like "*memurai*"}

# Check ports
netstat -ano | findstr "3000 5173 5432 6379"
```

### Start all services:
```powershell
# PostgreSQL
Start-Service postgresql-x64-14

# Memurai
Start-Service Memurai
```

### Stop all services:
```powershell
# PostgreSQL
Stop-Service postgresql-x64-14

# Memurai
Stop-Service Memurai
```

## 🔐 Security Notes

1. **Change default PostgreSQL password** in production
2. **Don't commit .env file** to version control
3. **Use strong passwords** for database
4. **Enable firewall rules** for production deployment

## 📚 Next Steps

After successful setup:

1. ✅ Verify backend is running: http://localhost:3000/health
2. ✅ Verify frontend is running: http://localhost:5173
3. ✅ Run tests: `npm test`
4. ✅ Read the [README.md](./README.md) for feature documentation
5. ✅ Read the [PRD.md](./PRD.md) for product requirements

## 🆘 Getting Help

If you encounter issues:

1. Check this troubleshooting guide
2. Check backend logs in the terminal
3. Check browser console for frontend errors
4. Verify all services are running
5. Check environment variables in `.env`

## ✅ Quick Checklist

Before running the app, ensure:

- [ ] Node.js 20+ installed
- [ ] PostgreSQL 14+ installed and running
- [ ] Redis/Memurai installed and running
- [ ] FFMPEG installed and in PATH
- [ ] Database `kasset_manager` created
- [ ] `.env` file configured
- [ ] Dependencies installed (`npm run install:all`)
- [ ] Migrations run (`npm run migrate`)
- [ ] Backend starts without errors
- [ ] Frontend starts without errors

## 🎉 Success!

If you can access http://localhost:5173 and see the KAsset Manager interface, you're all set!

