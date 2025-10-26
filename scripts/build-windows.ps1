param(
    [string]$QtPrefix="",
    [ValidateSet("Ninja","Visual Studio 17 2022")]
    [string]$Generator="Visual Studio 17 2022",
    [switch]$Package
)

$ErrorActionPreference = "Stop"

function Find-QtPrefix {
    param([string]$Hint)
    if ($Hint -and (Test-Path $Hint)) { return (Resolve-Path $Hint).Path }

    $candidates = @()
    if ($env:Qt6) { $candidates += "$env:Qt6" }
    if ($env:Qt6_DIR) { $candidates += "$env:Qt6_DIR" }
    if ($env:QT_DIR) { $candidates += "$env:QT_DIR" }
    $candidates += "C:/Qt"

    foreach ($root in $candidates) {
        if (-not (Test-Path $root)) { continue }
        $dirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '6*' -or $_.Name -like '5*' } |
            Sort-Object Name -Descending
        foreach ($d in $dirs) {
            # ALWAYS prefer MSVC over MinGW for better DLL compatibility
            $msvc2022 = Join-Path $d.FullName "msvc2022_64"
            $msvc2019 = Join-Path $d.FullName "msvc2019_64"
            if (Test-Path $msvc2022) {
                Write-Host "Found Qt with MSVC 2022" -ForegroundColor Green
                return $msvc2022
            }
            if (Test-Path $msvc2019) {
                Write-Host "Found Qt with MSVC 2019" -ForegroundColor Green
                return $msvc2019
            }
        }
    }
    throw "Qt with MSVC not found. Please install Qt 6 with MSVC 2022 or 2019 compiler."
}

function Initialize-QtToolchain {
    param([string]$QtPrefix)
    # Ensure Qt bin on PATH for moc, rcc, windeployqt, etc.
    $qtBin = Join-Path $QtPrefix 'bin'
    if (Test-Path $qtBin) {
        $env:PATH = "$qtBin;$env:PATH"
        Write-Host "Added Qt bin to PATH" -ForegroundColor Green
    }
    if (Test-Path 'C:/Qt/Tools/Ninja') {
        $env:PATH = "C:/Qt/Tools/Ninja;$env:PATH"
        Write-Host "Added Ninja to PATH" -ForegroundColor Green
    }
}


# Compute paths and initialize toolchain
$repoRoot = (Resolve-Path "$PSScriptRoot/.." ).Path
$src = Join-Path $repoRoot "native/qt6"
$buildBase = Join-Path $src "build"
if ($Generator -eq "Ninja") {
    $build = Join-Path $buildBase "ninja"
} else {
    $build = Join-Path $buildBase "vs2022"
}

if (-not $QtPrefix) { $QtPrefix = Find-QtPrefix -Hint $QtPrefix }
Write-Host "Using Qt prefix: $QtPrefix"
Initialize-QtToolchain -QtPrefix $QtPrefix



$configureArgs = @(
    "-S", $src,
    "-B", $build,
    "-G", $Generator,
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
)

cmake @configureArgs

# Build Release by default for packaging
$buildArgs = @("--build", $build)
if ($Generator -eq "Visual Studio 17 2022") {
    $buildArgs += @("--config","Release")
}
cmake @buildArgs

if ($Package) {
    Push-Location $build
    try {
        # 1) Install into a local staging dir and verify the app runs before packaging
        $stage = Join-Path $build 'install_run'
        if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
        $installArgs = @('--install', $build, '--prefix', $stage)
        if ($Generator -eq 'Visual Studio 17 2022') { $installArgs += @('--config','Release') }
        cmake @installArgs

        $exe = Join-Path $stage 'bin/kassetmanagerqt.exe'
        if (-not (Test-Path $exe)) { throw "Installed exe not found: $exe" }

        # 2) Copy ALL vcpkg DLLs BEFORE running verification (OpenImageIO has many dependencies)
        if ($Generator -eq 'Visual Studio 17 2022') {
            $vcpkgBin = 'C:\vcpkg\installed\x64-windows\bin'
            if (Test-Path $vcpkgBin) {
                Write-Host "Copying ALL vcpkg DLLs to staging directory..." -ForegroundColor Cyan
                $stageBinDir = Join-Path $stage 'bin'
                $dlls = Get-ChildItem -Path $vcpkgBin -Filter "*.dll" -ErrorAction SilentlyContinue
                $copiedCount = 0
                foreach ($dll in $dlls) {
                    Copy-Item $dll.FullName -Destination $stageBinDir -Force
                    $copiedCount++
                }
                Write-Host "Copied $copiedCount DLL files from vcpkg" -ForegroundColor Green
            } else {
                Write-Warning "vcpkg not found at $vcpkgBin - application may be missing required DLLs"
            }
        }

        # 3) Verify the app runs with all DLLs present
        Write-Host "Verifying application starts correctly..." -ForegroundColor Yellow
        $p = Start-Process -FilePath $exe -PassThru
        Start-Sleep -Seconds 4
        $running = $false
        try { $p.Refresh(); $running = -not $p.HasExited } catch {}
        if (-not $running) { throw "Verify run failed: process exited early. Check $stage/bin/startup.log" }
        # Stop after verification window
        $p | Stop-Process -Force
        Write-Host "Application verification successful" -ForegroundColor Green

        # 4) Copy verified staging to portable folder
        try { Get-Process -Name kassetmanagerqt -ErrorAction SilentlyContinue | Stop-Process -Force } catch {}
        $portable = Join-Path $repoRoot 'dist/portable'
        if (Test-Path $portable) { Remove-Item -Recurse -Force $portable }
        Copy-Item -Recurse -Force $stage $portable
        Write-Host "Portable distribution created" -ForegroundColor Green

        Write-Host ("UPDATED_PORTABLE:{0}" -f (Resolve-Path $portable))

        # 5) Package only after successful verify run
        $nsis = Get-Command makensis.exe -ErrorAction SilentlyContinue
        if ($Generator -eq 'Visual Studio 17 2022') {
            if ($nsis) { cpack -G NSIS -C Release } else { cpack -G ZIP -C Release }
        } else {
            if ($nsis) { cpack -G NSIS } else { cpack -G ZIP }
        }
    } finally {
        Pop-Location
    }
}

