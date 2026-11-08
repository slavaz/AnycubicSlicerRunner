#!/bin/bash

# === CONFIGURATION ===
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || cd "$SCRIPT_DIR/.." && pwd)"
CHECK_UPDATE_SCRIPT="$SCRIPT_DIR/check-update.sh"
VERSION_FILE="$PROJECT_ROOT/version.txt"
BACKUP_DIR="$PROJECT_ROOT/backups"
APP_DIR="$PROJECT_ROOT/app"
DEB_PACKAGES_DIR="$PROJECT_ROOT/deb-packages"
DESKTOP_FILE="$HOME/Desktop/AnycubicSlicer.desktop"
DESKTOP_ICON="$PROJECT_ROOT/anycubic-slicer-icon.png"
BUILD_VERSION_FILE="$APP_DIR/share/AnycubicSlicerNext/resources/build-version.txt"

# === FUNCTIONS ===
log() {
  [[ -t 1 ]] && echo "$@"
}

apply_fedora_fixes() {
  local fonts_dir="$APP_DIR/share/AnycubicSlicerNext/resources/fonts"
  if [[ -d "$fonts_dir" && ! -e "$fonts_dir/NotoSans-Regular.ttf" ]]; then
    if [[ -f "/usr/share/fonts/google-noto/NotoSans-Regular.ttf" ]]; then
      ln -s "/usr/share/fonts/google-noto/NotoSans-Regular.ttf" "$fonts_dir/NotoSans-Regular.ttf"
      log "Linked system NotoSans-Regular.ttf into $fonts_dir"
    fi
  fi

  local fix_src="$PROJECT_ROOT/workbench-fix/acnext-workbench-fix.c"
  local fix_so="$PROJECT_ROOT/workbench-fix/acnext-workbench-fix.so"
  if [[ -f "$fix_src" ]] && command -v gcc >/dev/null 2>&1; then
    gcc -shared -fPIC -O2 -o "$fix_so" "$fix_src" -ldl
    log "Built workbench MIME fix shim: $fix_so"
  fi
}

# === CREATE NEEDED DIRS ===
mkdir -p "$BACKUP_DIR" "$APP_DIR"

# === RUN CHECK FOR UPDATES ===
OUTPUT=$(bash "$CHECK_UPDATE_SCRIPT")

NEEDS_INSTALL=0
if echo "$OUTPUT" | grep -q "New version" && echo "$OUTPUT" | grep -q "saved to $VERSION_FILE"; then
  NEEDS_INSTALL=1
elif [[ -f "$VERSION_FILE" ]]; then
  EXPECTED_VER="$(sed 's/^[0-9]*://' "$VERSION_FILE" | tr -d '[:space:]')"
  DEPLOYED_VER="$(cat "$BUILD_VERSION_FILE" 2>/dev/null | tr -d '[:space:]' || true)"
  if [[ -n "$EXPECTED_VER" && "$EXPECTED_VER" != "$DEPLOYED_VER" ]] && compgen -G "$DEB_PACKAGES_DIR/*.deb" > /dev/null; then
    log "Detected downloaded version $EXPECTED_VER not yet deployed in app/ (currently: ${DEPLOYED_VER:-none}). Installing..."
    NEEDS_INSTALL=1
  fi
fi

if [[ "$NEEDS_INSTALL" -eq 1 ]]; then
  DATE=$(date +"%Y-%m-%d")
  BACKUP_PATH="$BACKUP_DIR/$DATE"

  if [[ -d "$BACKUP_PATH" ]]; then
    log "Backup for $DATE already exists. Skipping backup step."
  else
    log "Backing up current version to $BACKUP_PATH"
    mkdir -p "$BACKUP_PATH"
    mv "$APP_DIR"/* "$BACKUP_PATH"/ 2>/dev/null || true
  fi

  # sort by modification time — newest first — to avoid alphabetical ordering issues
  LATEST_DEB_FILE=$(ls -t "$DEB_PACKAGES_DIR"/*.deb | head -n 1)
  TEMP_DIR=$(mktemp -d)

  log "Extracting $LATEST_DEB_FILE using ar/tar"
  ar x "$LATEST_DEB_FILE" --output="$TEMP_DIR" >/dev/null 2>&1
  DATA_FILE=$(find "$TEMP_DIR" -type f -name "data.tar.*" | head -n 1)
  if [[ -z "$DATA_FILE" ]]; then
    log "Error: data.tar not found in package."
    rm -rf "$TEMP_DIR"
    exit 1
  fi

  # Clean the app dir first to avoid directory conflicts
  rm -rf "$APP_DIR"/*
  tar --strip-components=2 -xf "$DATA_FILE" -C "$APP_DIR" ./usr >/dev/null 2>&1

  rm -rf "$TEMP_DIR"

  apply_fedora_fixes

  # === REFRESH DESKTOP ENTRY ===
  log "Refreshing desktop shortcut"
  cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Name=Anycubic Slicer
Comment=Launch Anycubic Slicer
Exec=$PROJECT_ROOT/AnycubicSlicer.sh
Icon=$DESKTOP_ICON
Path=$PROJECT_ROOT
Terminal=false
Type=Application
Categories=Graphics;3DGraphics;
StartupNotify=true
EOF

  chmod +x "$DESKTOP_FILE"
  log "Desktop entry updated: $DESKTOP_FILE"
else
  apply_fedora_fixes
  log "No updates found."
fi
