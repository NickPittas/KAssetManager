# KAsset Manager - Professional Windows Installer Plan

## Overview

Create a **single-click Windows installer** that includes ALL dependencies and requires ZERO manual setup from the user.

## Installer Requirements

### What the Installer MUST Include

1. **Application Files**
   - Electron executable
   - Backend (NestJS) bundled
   - Frontend (React) bundled
   - All Node.js dependencies bundled

2. **Embedded Database**
   - PostgreSQL portable version (no separate installation)
   - Pre-configured and ready to use
   - Runs as Windows service or background process

3. **Embedded Redis**
   - Redis portable version for Windows
   - Pre-configured and ready to use
   - Runs as Windows service or background process

4. **FFMPEG Binaries**
   - FFMPEG.exe bundled
   - FFprobe.exe bundled
   - All required DLLs included

5. **Configuration**
   - Settings stored in app data folder
   - User preferences in SQLite or JSON
   - NO .env files required from user

### Installation Process

```
1. User downloads: KAssetManager-Setup-1.0.0.exe (single file)
2. User runs installer
3. Installer shows welcome screen
4. User chooses installation directory (default: C:\Program Files\KAsset Manager)
5. Installer extracts all files
6. Installer sets up embedded PostgreSQL
7. Installer sets up embedded Redis
8. Installer creates database schema
9. Installer creates desktop shortcut
10. Installer creates Start Menu entry
11. Installation complete - app ready to use
```

### Uninstallation Process

```
1. User runs uninstaller from Control Panel or Start Menu
2. Uninstaller stops all services
3. Uninstaller asks: "Keep user data?" (Yes/No)
4. If No: Delete database and cache
5. If Yes: Keep data for reinstallation
6. Uninstaller removes all files
7. Uninstaller removes shortcuts
8. Uninstallation complete
```

## Technical Implementation

### Technology Stack

1. **Electron Builder** - Main packaging tool
2. **NSIS (Nullsoft Scriptable Install System)** - Windows installer
3. **node-windows** - Windows service management
4. **PostgreSQL Portable** - Embedded database
5. **Redis Portable** - Embedded cache

### File Structure

```
KAssetManager-Setup-1.0.0.exe (installer)
│
└── Extracts to: C:\Program Files\KAsset Manager\
    ├── KAsset Manager.exe (Electron app)
    ├── resources\
    │   ├── app.asar (bundled application)
    │   ├── postgresql\ (embedded PostgreSQL)
    │   │   ├── bin\
    │   │   ├── lib\
    │   │   └── data\ (database files)
    │   ├── redis\ (embedded Redis)
    │   │   ├── redis-server.exe
    │   │   └── redis.conf
    │   └── ffmpeg\
    │       ├── ffmpeg.exe
    │       ├── ffprobe.exe
    │       └── *.dll
    ├── backend\ (NestJS compiled)
    ├── frontend\ (React compiled)
    └── uninstall.exe

User Data: C:\Users\{Username}\AppData\Local\KAsset Manager\
├── config.json (user settings)
├── cache\ (thumbnails)
└── logs\
```

### Configuration Management

**NO .env files!** All configuration managed through:

1. **config.json** (user settings)
```json
{
  "cacheDir": "C:\\Users\\{Username}\\KAssets\\cache",
  "cacheQuotaGB": 10,
  "llmEnabled": false,
  "llmEndpoint": "http://localhost:11434",
  "telemetryEnabled": true,
  "theme": "dark",
  "language": "en"
}
```

2. **Internal environment** (set by app at runtime)
```javascript
process.env.DB_HOST = 'localhost';
process.env.DB_PORT = '54320'; // Custom port to avoid conflicts
process.env.DB_NAME = 'kasset_manager';
process.env.DB_USER = 'kasset_user';
process.env.DB_PASSWORD = generateSecurePassword(); // Generated at install
process.env.REDIS_HOST = 'localhost';
process.env.REDIS_PORT = '63790'; // Custom port to avoid conflicts
```

## Implementation Steps

### Phase 1: Prepare Portable Dependencies

#### 1.1 PostgreSQL Portable
- Download PostgreSQL portable version
- Configure for embedded use
- Create initialization scripts
- Bundle with installer

#### 1.2 Redis Portable
- Download Redis for Windows
- Configure for embedded use
- Create Windows service wrapper
- Bundle with installer

#### 1.3 FFMPEG Binaries
- Download FFMPEG static build
- Include all required DLLs
- Bundle with installer

### Phase 2: Electron Configuration

#### 2.1 Main Process
- Create electron/main.ts
- Manage child processes (PostgreSQL, Redis, Backend)
- Handle app lifecycle
- Manage system tray

#### 2.2 Service Management
- Start PostgreSQL on app launch
- Start Redis on app launch
- Start backend server on app launch
- Stop all services on app quit

#### 2.3 Auto-Update
- Configure electron-updater
- Check for updates on launch
- Download and install updates
- Notify user of updates

### Phase 3: Installer Configuration

#### 3.1 Electron Builder Config
```javascript
{
  "appId": "com.kassetmanager.app",
  "productName": "KAsset Manager",
  "directories": {
    "output": "dist-installer"
  },
  "files": [
    "dist/**/*",
    "resources/**/*"
  ],
  "extraResources": [
    {
      "from": "resources/postgresql",
      "to": "postgresql"
    },
    {
      "from": "resources/redis",
      "to": "redis"
    },
    {
      "from": "resources/ffmpeg",
      "to": "ffmpeg"
    }
  ],
  "win": {
    "target": ["nsis"],
    "icon": "build/icon.ico"
  },
  "nsis": {
    "oneClick": false,
    "allowToChangeInstallationDirectory": true,
    "createDesktopShortcut": true,
    "createStartMenuShortcut": true,
    "shortcutName": "KAsset Manager",
    "include": "build/installer.nsh"
  }
}
```

#### 3.2 NSIS Custom Script
- Initialize PostgreSQL database
- Create Windows services
- Set up firewall rules
- Create uninstaller

### Phase 4: First-Run Experience

#### 4.1 Welcome Screen
- Show welcome message
- Quick setup wizard
- Choose cache location
- Choose library paths

#### 4.2 Database Initialization
- Create database schema
- Run migrations
- Create default collections
- Set up indexes

#### 4.3 Settings
- All settings in UI
- No manual file editing
- Import/export settings

## Installer Size Estimate

| Component | Size |
|-----------|------|
| Electron + App | ~150 MB |
| PostgreSQL Portable | ~50 MB |
| Redis Portable | ~5 MB |
| FFMPEG | ~80 MB |
| **Total** | **~285 MB** |

## User Experience

### Installation (2-3 minutes)
1. Download installer (285 MB)
2. Run installer
3. Click "Next" a few times
4. Installation completes
5. Launch app
6. Welcome wizard (30 seconds)
7. Start using app

### Daily Use
1. Double-click desktop icon
2. App launches (5-10 seconds)
3. All services start automatically
4. Ready to use

### Uninstallation (1 minute)
1. Control Panel → Uninstall
2. Choose to keep or delete data
3. Uninstallation completes
4. All services stopped and removed

## Benefits

✅ **Zero manual setup** - Everything included  
✅ **No conflicts** - Custom ports, isolated installation  
✅ **Professional** - Works like any Windows app  
✅ **Clean uninstall** - Removes everything properly  
✅ **Auto-updates** - Keep app up to date  
✅ **User-friendly** - No technical knowledge required  
✅ **Portable** - Can be installed on any Windows 10/11 machine  

## Next Steps

1. **Download portable dependencies**
   - PostgreSQL portable
   - Redis for Windows
   - FFMPEG static build

2. **Create Electron main process**
   - Process management
   - Service lifecycle
   - System tray integration

3. **Configure Electron Builder**
   - Package configuration
   - NSIS scripts
   - Code signing

4. **Test installer**
   - Clean Windows VM
   - Install/uninstall cycle
   - Verify all features work

5. **Create auto-update server**
   - Host update files
   - Configure update checks
   - Test update process

## Timeline Estimate

- **Phase 1** (Portable Dependencies): 1 day
- **Phase 2** (Electron Configuration): 2 days
- **Phase 3** (Installer Configuration): 2 days
- **Phase 4** (First-Run Experience): 1 day
- **Testing & Polish**: 2 days

**Total**: 8 days of focused work

## Deliverables

1. ✅ `KAssetManager-Setup-1.0.0.exe` - Single installer file
2. ✅ Desktop shortcut after installation
3. ✅ Start Menu entry
4. ✅ Uninstaller in Control Panel
5. ✅ Auto-update functionality
6. ✅ User documentation
7. ✅ Installation guide (for reference only)

