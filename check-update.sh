#!/bin/bash


# Get project root using git
PROJECT_ROOT="$(git rev-parse --show-toplevel)"
# URL to the repository metadata
REPO_URL="https://cdn-universe-slicer.anycubic.com/prod/dists/noble/main/binary-amd64/Packages"
# Directory to store downloaded .deb packages
DOWNLOAD_DIR="$PROJECT_ROOT/deb-packages"
# Version file path
VERSION_FILE="$PROJECT_ROOT/version.txt"
# Default version if version.txt does not exist
DEFAULT_VERSION="1.0.0"

# Check if the download directory exists; if not, create it
if [ ! -d "$DOWNLOAD_DIR" ]; then
  mkdir -p "$DOWNLOAD_DIR"
fi

# Check if version.txt exists
if [ -f "$VERSION_FILE" ]; then
  # Read the installed version from the version.txt file
  INSTALLED_VERSION=$(cat "$VERSION_FILE")
else
  # If version.txt doesn't exist, assume the default version
  INSTALLED_VERSION="$DEFAULT_VERSION"
  echo "version.txt not found, assuming installed version: $INSTALLED_VERSION"
fi

# Fetch the package metadata
PACKAGE_METADATA=$(curl -s "$REPO_URL")

# Extract the latest version available from the metadata
LATEST_VERSION=$(echo "$PACKAGE_METADATA" | grep -oP "(?<=Version: )\S+")

# Extract the filename of the latest .deb package
PACKAGE_FILE=$(echo "$PACKAGE_METADATA" | grep -oP "(?<=Filename: )\S+")

# Check if the latest version is greater than the installed version
if [[ "$LATEST_VERSION" != "$INSTALLED_VERSION" ]]; then
  echo "New version detected: $LATEST_VERSION (installed version: $INSTALLED_VERSION)"
  echo "Downloading new version..."

  # Download the new version of the .deb package to the download directory
  curl -o "$DOWNLOAD_DIR/$(basename "$PACKAGE_FILE")" "https://cdn-universe-slicer.anycubic.com/prod/$PACKAGE_FILE"

  # Write the new version to version.txt
  echo "$LATEST_VERSION" > "$VERSION_FILE"

  echo "Download completed: $DOWNLOAD_DIR/$(basename "$PACKAGE_FILE")"
  echo "New version ($LATEST_VERSION) saved to $VERSION_FILE"
else
  echo "You already have the latest version: $INSTALLED_VERSION"
fi
