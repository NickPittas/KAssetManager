param(
    [string]$QtPrefix="",
    [ValidateSet("Ninja","Visual Studio 17 2022")]
    [string]$Generator="Ninja",
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
            $msvc2022 = Join-Path $d.FullName "msvc2022_64"
            $msvc2019 = Join-Path $d.FullName "msvc2019_64"
            if (Test-Path $msvc2022) { return $msvc2022 }
            if (Test-Path $msvc2019) { return $msvc2019 }
            $mingw64 = Join-Path $d.FullName "mingw_64"
            if (Test-Path $mingw64) { return $mingw64 }
            $mingwAny = Get-ChildItem -Path $d.FullName -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'mingw*' } | Select-Object -First 1
            if ($mingwAny) { return $mingwAny.FullName }
        }
    }
    throw "Qt prefix not found. Install Qt 6 and pass -QtPrefix or set Qt6/Qt6_DIR/QT_DIR env var."
}

function Initialize-QtToolchain {
    param([string]$QtPrefix)
    if ($QtPrefix -match 'mingw') {
        $tool = Get-ChildItem -Path 'C:/Qt/Tools' -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'mingw*' } | Sort-Object Name -Descending | Select-Object -First 1
        if ($tool) {
            $bin = Join-Path $tool.FullName 'bin'
            $env:PATH = "$bin;$env:PATH"
            $env:CC   = Join-Path $bin 'gcc.exe'
            $env:CXX  = Join-Path $bin 'g++.exe'
            Write-Host "Configured MinGW toolchain: $($env:CC)"
        } else {
            Write-Warning 'MinGW toolchain not found under C:\Qt\Tools. Build may fail.'
        }
    }
    # Ensure Qt bin and Ninja on PATH
    $qtBin = Join-Path $QtPrefix 'bin'
    if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
    if (Test-Path 'C:/Qt/Tools/Ninja') {
        $env:PATH = "C:/Qt/Tools/Ninja;$env:PATH"
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

        $p = Start-Process -FilePath $exe -PassThru
        Start-Sleep -Seconds 4
        $running = $false
        try { $p.Refresh(); $running = -not $p.HasExited } catch {}
        if (-not $running) { throw "Verify run failed: process exited early. Check $stage/bin/startup.log" }
        # Stop after verification window
        $p | Stop-Process -Force

        # 2) ALWAYS refresh portable folder from the verified install_run
        try { Get-Process -Name kassetmanagerqt -ErrorAction SilentlyContinue | Stop-Process -Force } catch {}
        $portable = Join-Path $repoRoot 'dist/portable'
        if (Test-Path $portable) { Remove-Item -Recurse -Force $portable }
        Copy-Item -Recurse -Force $stage $portable

        # Copy OpenImageIO DLLs from vcpkg if using MSVC
        if ($Generator -eq 'Visual Studio 17 2022') {
            $vcpkgBin = 'C:\vcpkg\installed\x64-windows\bin'
            if (Test-Path $vcpkgBin) {
                Write-Host "Copying OpenImageIO DLLs from vcpkg..."
                Copy-Item "$vcpkgBin\*.dll" -Destination (Join-Path $portable 'bin') -Force -ErrorAction SilentlyContinue
            }
        }

        Write-Host ("UPDATED_PORTABLE:{0}" -f (Resolve-Path $portable))

        # 3) Package only after successful verify run
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

