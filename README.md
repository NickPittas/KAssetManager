# KAsset Manager

Desktop asset manager for local/NAS video & photo libraries with LLM-assisted descriptions, user-defined tagging, and non-destructive organization.

## 🏗️ Architecture

- **Frontend**: React 18 + TypeScript + Vite + Tailwind CSS + Redux Toolkit
- **Backend**: NestJS + TypeScript + PostgreSQL + Redis + BullMQ
- **Media Processing**: FFMPEG with GPU acceleration support
- **Desktop**: Electron 28 (planned for production)

## 📋 Prerequisites

Before you begin, ensure you have the following installed:

- **Node.js 20+** - [Download](https://nodejs.org/)
- **PostgreSQL 14+** - [Download](https://www.postgresql.org/download/)
- **Redis 7+** - [Download](https://redis.io/download/)
- **FFMPEG** - [Download](https://ffmpeg.org/download.html)
- **Git** - [Download](https://git-scm.com/)

### Windows-Specific Prerequisites

1. **PostgreSQL**: Install from [official installer](https://www.postgresql.org/download/windows/)
2. **Redis**: Install via [Memurai](https://www.memurai.com/) or [Redis for Windows](https://github.com/microsoftarchive/redis/releases)
3. **FFMPEG**: Download from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) and add to PATH

## ⚠️ IMPORTANT: Current Status

**Code Status**: ✅ Complete and compiles successfully
**Runtime Status**: ⚠️ Requires setup of external dependencies
**Testing Status**: ❌ Not fully tested end-to-end
**Installer Status**: ❌ Not implemented

**Before you can run the application, you MUST**:
1. Install PostgreSQL 14+ and create database
2. Install Redis/Memurai and start service
3. Install FFMPEG and add to PATH
4. Run database migrations

See [SETUP-WINDOWS.md](./SETUP-WINDOWS.md) for complete setup instructions.

## 🚀 Quick Start (After Prerequisites Installed)

### Automated Setup (Recommended for First Time)

```powershell
# Run this ONCE to set up everything
.\setup-windows.ps1
```

This script will:
1. Check prerequisites
2. Start PostgreSQL and Redis services
3. Create database
4. Install dependencies
5. Configure environment
6. Run migrations
7. Build projects

### Quick Start (After Setup Complete)

```powershell
# Run this to start the application
.\start.ps1

# OR use npm command
npm run dev:all
```

This will start:
- Backend API on <http://localhost:3000>
- Frontend dev server on <http://localhost:5173>

### Production Build

```bash
npm run build:all
```

This will build both backend and frontend for production.

## 📦 Detailed Setup Instructions

### Step 1: Clone and Install Dependencies

```bash
# Clone the repository
git clone <repository-url>
cd KAssetManager

# Install all dependencies (backend + frontend)
npm run install:all
```

### Step 2: Database Setup

#### PostgreSQL Setup

```bash
# Create database
createdb kasset_manager

# Or using psql
psql -U postgres
CREATE DATABASE kasset_manager;
\q
```

#### Redis Setup

```bash
# Start Redis (Linux/Mac)
redis-server

# Start Redis (Windows with Memurai)
memurai
```

### Step 3: Environment Configuration

```bash
# Copy environment template
cp backend/.env.example backend/.env

# Edit the .env file with your settings
```

**Required Environment Variables:**

```env
# Database
DB_HOST=localhost
DB_PORT=5432
DB_NAME=kasset_manager
DB_USER=postgres
DB_PASSWORD=your_password

# Redis
REDIS_HOST=localhost
REDIS_PORT=6379

# Application
PORT=3000
NODE_ENV=development

# Cache
CACHE_DIR=C:\Users\YourUser\KAssets\cache
CACHE_QUOTA_GB=10

# LLM (Optional)
LLM_ENDPOINT=http://localhost:11434
LLM_MODEL=qwen2-vl:7b

# Telemetry (Optional)
TELEMETRY_ENABLED=true

# Backup
BACKUP_DIR=./backups
```

### Step 4: Run Database Migrations

```bash
npm run migrate
```

### Step 5: Start Development Servers

```bash
# Start everything with one command
npm run dev:all

# Or start individually:
npm run dev:backend    # Backend only (http://localhost:3000)
npm run dev:frontend   # Frontend only (http://localhost:5173)
```

## 🧪 Testing the Application

### 1. Verify Backend is Running

```bash
# Test health endpoint
curl http://localhost:3000/health

# Expected response: {"status":"ok"}
```

### 2. Verify Frontend is Running

Open browser to: http://localhost:5173

You should see the KAsset Manager interface.

### 3. Run Tests

```bash
# Run all tests
npm test

# Run backend tests only
npm run test:backend

# Run frontend tests only
npm run test:frontend

# Run e2e tests
npm run test:e2e
```

## 📦 Building for Production

### Build Everything

```bash
npm run build:all
```

### Build Individually

```bash
npm run build:backend   # Builds to backend/dist
npm run build:frontend  # Builds to frontend/dist
```

## 🎁 Creating Installer (NOT YET IMPLEMENTED)

**Status**: ⚠️ Installer creation is planned but not yet implemented.

**Planned Implementation:**
- Electron Builder for packaging
- FFMPEG bundled with installer
- Auto-update support
- Windows/Mac/Linux installers

**To implement installer, you would need to:**
1. Set up Electron main process
2. Configure Electron Builder
3. Bundle FFMPEG binaries
4. Create installer scripts

## 🔧 Available NPM Scripts

### Root Level Scripts

```bash
npm run install:all      # Install all dependencies
npm run dev:all          # Start backend + frontend in dev mode
npm run build:all        # Build backend + frontend for production
npm run test             # Run all tests
npm run migrate          # Run database migrations
npm run clean            # Clean all build artifacts
```

### Backend Scripts

```bash
npm run dev:backend      # Start backend in dev mode
npm run build:backend    # Build backend
npm run test:backend     # Run backend tests
npm run migrate:backend  # Run migrations
```

### Frontend Scripts

```bash
npm run dev:frontend     # Start frontend in dev mode
npm run build:frontend   # Build frontend
npm run test:frontend    # Run frontend tests
```

## 🐛 Troubleshooting

### PostgreSQL Connection Issues

```bash
# Check if PostgreSQL is running
pg_isready

# Check connection
psql -U postgres -d kasset_manager -c "SELECT 1"
```

### Redis Connection Issues

```bash
# Check if Redis is running
redis-cli ping

# Expected response: PONG
```

### Port Already in Use

```bash
# Kill process on port 3000 (backend)
npx kill-port 3000

# Kill process on port 5173 (frontend)
npx kill-port 5173
```

### Database Migration Errors

```bash
# Drop and recreate database
dropdb kasset_manager
createdb kasset_manager
npm run migrate
```

## 📚 Project Structure

```
KAssetManager/
├── backend/                 # NestJS backend
│   ├── src/
│   │   ├── entities/       # TypeORM entities
│   │   ├── modules/        # Feature modules
│   │   ├── app.module.ts   # Root module
│   │   └── main.ts         # Entry point
│   ├── test/               # E2E tests
│   └── package.json
├── frontend/               # React frontend
│   ├── src/
│   │   ├── components/     # React components
│   │   ├── pages/          # Page components
│   │   ├── hooks/          # Custom hooks
│   │   ├── utils/          # Utilities
│   │   ├── i18n/           # Internationalization
│   │   └── App.tsx         # Root component
│   └── package.json
├── package.json            # Root package.json
└── README.md               # This file
```

## ✅ Implementation Status

### Completed Features (25/25 tasks)

- ✅ Core backend infrastructure
- ✅ Database schema and migrations
- ✅ Ingestion pipeline with checksum dedupe
- ✅ FFMPEG integration and media processing
- ✅ Thumbnail cache system
- ✅ LLM integration (Ollama/LM Studio)
- ✅ Search and filter engine
- ✅ Frontend layout and navigation
- ✅ Asset grid/list virtualization
- ✅ Metadata editing UI
- ✅ Drag and drop workflows
- ✅ Collections management
- ✅ Settings and configuration UI
- ✅ Multi-user collaboration
- ✅ Audit logging and history
- ✅ Telemetry and diagnostics
- ✅ Backup and restore
- ✅ Accessibility and internationalization
- ✅ Testing and validation

### Not Yet Implemented

- ⚠️ Electron desktop packaging
- ⚠️ Installer creation
- ⚠️ Auto-update mechanism
- ⚠️ FFMPEG bundling

## 📖 Documentation

- [PRD.md](./PRD.md) - Product Requirements Document
- [API Documentation](http://localhost:3000/api) - Swagger API docs (when backend is running)

## 🤝 Contributing

This is a private project. Contact the maintainer for contribution guidelines.

## 📄 License

MIT

