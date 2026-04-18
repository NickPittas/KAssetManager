#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build-linux-appimage}"
appdir_root="${APPDIR:-$build_dir/AppDir}"
usr_dir="$appdir_root/usr"
linux_packaging_dir="$repo_root/native/qt6/packaging/linux"
desktop_file="$linux_packaging_dir/kassetmanager.desktop"
icon_file="$repo_root/icon.png"
appimage_name="${APPIMAGE_NAME:-KAssetManager-$(uname -m).AppImage}"

prepend_path() {
  var_name="$1"
  value="$2"
  eval "current=\${$var_name-}"
  if [[ -n "$current" ]]; then
    eval "export $var_name=\"$value:$current\""
  else
    eval "export $var_name=\"$value\""
  fi
}

copy_tree_if_present() {
  src="$1"
  dst="$2"
  if [[ -d "$src" ]]; then
    mkdir -p "$dst"
    cp -a "$src/." "$dst/"
  fi
}

if [[ ! -d "$usr_dir" ]]; then
  printf 'Missing AppDir staging at %s\nRun scripts/build-linux-appimage.sh first.\n' "$usr_dir" >&2
  exit 1
fi

if [[ ! -f "$desktop_file" ]]; then
  printf 'Missing desktop file: %s\n' "$desktop_file" >&2
  exit 1
fi

if [[ ! -f "$icon_file" ]]; then
  printf 'Missing icon file: %s\n' "$icon_file" >&2
  exit 1
fi

if [[ ! -f "$usr_dir/bin/kassetmanagerqt" ]]; then
  printf 'Missing installed executable at %s\n' "$usr_dir/bin/kassetmanagerqt" >&2
  exit 1
fi

appimagetool_bin="${APPIMAGETOOL:-$(command -v appimagetool || true)}"
linuxdeploy_bin="${LINUXDEPLOY:-$(command -v linuxdeploy || true)}"
linuxdeploy_plugin_qt_bin="${LINUXDEPLOY_PLUGIN_QT:-$(command -v linuxdeploy-plugin-qt || true)}"

app_run="$appdir_root/AppRun"
cat > "$app_run" <<'EOF'
#!/bin/sh
set -e

APPDIR="${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}"
ENV_FILE="$APPDIR/usr/share/kassetmanager/AppRun.env"

if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ENV_FILE"
fi

exec "$APPDIR/usr/bin/kassetmanagerqt" "$@"
EOF
chmod +x "$app_run"

cp "$desktop_file" "$appdir_root/kassetmanager.desktop"
cp "$icon_file" "$appdir_root/kassetmanager.png"

if [[ -n "$linuxdeploy_bin" && -n "$linuxdeploy_plugin_qt_bin" ]]; then
  export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
  export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqxcb.so"
  prepend_path LD_LIBRARY_PATH "$usr_dir/lib"
  "$linuxdeploy_bin" \
    --appdir "$appdir_root" \
    --desktop-file "$desktop_file" \
    --icon-file "$icon_file" \
    --plugin qt
else
  qtpaths_bin="$(command -v qtpaths6 || command -v qtpaths || true)"
  if [[ -n "$qtpaths_bin" ]]; then
    qt_plugins_dir="$($qtpaths_bin --query QT_INSTALL_PLUGINS)"
    qt_qml_dir="$($qtpaths_bin --query QT_INSTALL_QML 2>/dev/null || true)"
    copy_tree_if_present "$qt_plugins_dir/platforms" "$usr_dir/plugins/platforms"
    copy_tree_if_present "$qt_plugins_dir/imageformats" "$usr_dir/plugins/imageformats"
    copy_tree_if_present "$qt_plugins_dir/iconengines" "$usr_dir/plugins/iconengines"
    copy_tree_if_present "$qt_plugins_dir/styles" "$usr_dir/plugins/styles"
    copy_tree_if_present "$qt_plugins_dir/sqldrivers" "$usr_dir/plugins/sqldrivers"
    copy_tree_if_present "$qt_plugins_dir/wayland-decoration-client" "$usr_dir/plugins/wayland-decoration-client"
    copy_tree_if_present "$qt_plugins_dir/wayland-graphics-integration-client" "$usr_dir/plugins/wayland-graphics-integration-client"
    copy_tree_if_present "$qt_plugins_dir/wayland-shell-integration" "$usr_dir/plugins/wayland-shell-integration"
    copy_tree_if_present "$qt_plugins_dir/multimedia" "$usr_dir/plugins/multimedia"
    copy_tree_if_present "$qt_qml_dir" "$usr_dir/qml"
    printf 'linuxdeploy not found; copied Qt plugins from %s via qtpaths.\n' "$qt_plugins_dir" >&2
  else
    printf 'linuxdeploy and qtpaths not found; leaving Qt plugin harvesting unresolved.\n' >&2
  fi
fi

output_path="$repo_root/$appimage_name"
if [[ -z "$appimagetool_bin" ]]; then
  printf 'appimagetool not found. AppDir is prepared at %s, but final AppImage creation is blocked.\n' "$appdir_root" >&2
  exit 1
fi

ARCH="${ARCH:-$(uname -m)}" "$appimagetool_bin" "$appdir_root" "$output_path"
printf 'Created %s\n' "$output_path"
