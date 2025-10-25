# Installation Guide

## Prerequisites

### Required Software

1. **Qt 6.9.3** (or later)
   - Download from: https://www.qt.io/download
   - Install components:
     - Qt 6.9.3 for MinGW 13.1.0 64-bit
     - Qt Multimedia
     - Qt SQL
     - Qt Widgets
   - Default installation path: `C:\Qt\6.9.3\mingw_64`

2. **MinGW 13.1.0** (64-bit)
   - Included with Qt installer
   - Default path: `C:\Qt\Tools\mingw1310_64`

3. **CMake 3.21** or later
   - Download from: https://cmake.org/download/
   - Add to PATH during installation

4. **Ninja Build System**
   - Download from: https://github.com/ninja-build/ninja/releases
   - Extract `ninja.exe` to a directory in PATH
   - Or use Qt's bundled Ninja: `C:\Qt\Tools\Ninja`

### Optional Software

- **Git** - For version control
- **Visual Studio Code** - Recommended IDE with C++ extensions

## Building from Source

### 1. Clone the Repository

```powershell
git clone https://github.com/YourOrg/KAssetManager.git
cd KAssetManager
```

### 2. Build the Application

The build script automatically configures CMake, compiles the application, and creates a portable package.

```powershell
# Full build with packaging
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package

# Build only (no packaging)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja
```

### 3. Build Output

After successful build:

- **Executable**: `dist/portable/bin/kassetmanagerqt.exe`
- **Package**: `native/qt6/build/ninja/KAsset Manager Qt-0.1.0-win64.zip`
- **Build files**: `native/qt6/build/ninja/`

### 4. Run the Application

```powershell
.\dist\portable\bin\kassetmanagerqt.exe
```

## Build Script Options

The `build-windows.ps1` script supports the following options:

```powershell
# Use Ninja generator (recommended, faster)
-Generator Ninja

# Use Visual Studio generator
-Generator "Visual Studio 17 2022"

# Create portable package
-Package

# Clean build (removes build directory first)
-Clean
```

### Examples

```powershell
# Clean build with Ninja and packaging
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Clean -Package

# Build with Visual Studio generator
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator "Visual Studio 17 2022"
```

## Manual Build (Advanced)

If you prefer to build manually without the script:

```powershell
# Navigate to Qt6 directory
cd native/qt6

# Create build directory
mkdir build/ninja
cd build/ninja

# Configure with CMake
cmake ../.. -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 -DCMAKE_BUILD_TYPE=Release

# Build
ninja

# Install to dist/portable
cmake --install . --prefix ../../install_run
```

## Troubleshooting

### Qt Not Found

**Error**: `Could not find Qt6`

**Solution**: Set `CMAKE_PREFIX_PATH` to your Qt installation:

```powershell
$env:CMAKE_PREFIX_PATH="C:\Qt\6.9.3\mingw_64"
```

Or edit `scripts/build-windows.ps1` and update the Qt path.

### MinGW Not Found

**Error**: `Could not find compiler`

**Solution**: Add MinGW to PATH:

```powershell
$env:PATH="C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
```

### Ninja Not Found

**Error**: `Could not find Ninja`

**Solution**: Install Ninja or add Qt's Ninja to PATH:

```powershell
$env:PATH="C:\Qt\Tools\Ninja;$env:PATH"
```

### Build Fails with "Permission Denied"

**Solution**: Close any running instances of the application and try again.

### Missing DLLs When Running

**Error**: Application fails to start due to missing Qt DLLs

**Solution**: The build script automatically runs `windeployqt` to copy required DLLs. If this fails:

```powershell
cd dist/portable/bin
C:\Qt\6.9.3\mingw_64\bin\windeployqt.exe kassetmanagerqt.exe
```

## Development Setup

### IDE Configuration (Visual Studio Code)

1. Install extensions:
   - C/C++ (Microsoft)
   - CMake Tools
   - Qt tools

2. Open workspace: `File > Open Folder > KAssetManager`

3. Configure CMake:
   - Press `Ctrl+Shift+P`
   - Select "CMake: Configure"
   - Choose "Ninja" as generator

4. Build:
   - Press `F7` or use "CMake: Build"

### Debugging

To debug the application:

1. Build in Debug mode:
   ```powershell
   cmake ../.. -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 -DCMAKE_BUILD_TYPE=Debug
   ```

2. Run with debugger:
   - Visual Studio Code: Press `F5`
   - Or attach to running process

## Deployment

### Creating a Portable Package

The `-Package` option creates a ZIP file with all dependencies:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
```

Output: `native/qt6/build/ninja/KAsset Manager Qt-0.1.0-win64.zip`

### Distribution

The portable package includes:
- Application executable
- Qt runtime DLLs
- Required plugins (platforms, multimedia, etc.)
- Data directory (created on first run)

Users can extract and run without installation.

## System Requirements

### Minimum

- Windows 10 (64-bit)
- 4 GB RAM
- 500 MB disk space
- DirectX 11 compatible GPU

### Recommended

- Windows 11 (64-bit)
- 8 GB RAM
- 1 GB disk space (plus space for assets)
- Dedicated GPU for video playback

## Database Location

The SQLite database is created at:
- **Portable**: `<app_directory>/data/kasset.db`
- **Installed**: `%APPDATA%/KAsset/data/kasset.db`

## Updating

To update to a new version:

1. Pull latest changes: `git pull`
2. Clean build: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Clean -Package`
3. Database migrations are applied automatically on first run

## Uninstalling

For portable version:
- Delete the application folder
- Optionally delete `data/kasset.db` to remove all data

## Support

For build issues or questions:
- Check [TECH.md](TECH.md) for architecture details
- Review [TASKS.md](TASKS.md) for known issues
- Contact the development team

