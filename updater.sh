#!/bin/bash

# === CONFIGURATION ===
PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
CHECK_UPDATE_SCRIPT="$PROJECT_ROOT/check-update.sh"
VERSION_FILE="$PROJECT_ROOT/version.txt"
BACKUP_DIR="$PROJECT_ROOT/backups"
APP_DIR="$PROJECT_ROOT/app"
DEB_PACKAGES_DIR="$PROJECT_ROOT/deb-packages"
DESKTOP_FILE="$HOME/Desktop/AnycubicSlicer.desktop"
DESKTOP_ICON="$PROJECT_ROOT/anycubic-slicer-icon.png"

# === FUNCTIONS ===
log() {
  [[ -t 1 ]] && echo "$@"
}

# === CREATE NEEDED DIRS ===
mkdir -p "$BACKUP_DIR" "$APP_DIR"

# === RUN CHECK FOR UPDATES ===
OUTPUT=$(bash "$CHECK_UPDATE_SCRIPT")

if echo "$OUTPUT" | grep -q "New version" && echo "$OUTPUT" | grep -q "saved to $VERSION_FILE"; then
  DATE=$(date +"%Y-%m-%d")
  BACKUP_PATH="$BACKUP_DIR/$DATE"

  if [[ -d "$BACKUP_PATH" ]]; then
    log "Backup for $DATE already exists. Skipping backup step."
  else
    log "Backing up current version to $BACKUP_PATH"
    mkdir -p "$BACKUP_PATH"
    mv "$APP_DIR"/* "$BACKUP_PATH"/ 2>/dev/null || true
  fi

  LATEST_DEB_FILE=$(ls "$DEB_PACKAGES_DIR"/*.deb | sort | tail -n 1)
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
  log "No updates found."
fi
