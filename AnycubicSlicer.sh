#!/bin/bash


# Get project root using git
PROJECT_ROOT="$(git rev-parse --show-toplevel)"
INSTALL_DIR="$PROJECT_ROOT/app"
LIB_DIR="$INSTALL_DIR/lib"

# Add the library directory to LD_LIBRARY_PATH so the system knows where to find the libraries
export LD_LIBRARY_PATH="$LIB_DIR:$LD_LIBRARY_PATH"

# Run the Anycubic Slicer executable
"$INSTALL_DIR/bin/AnycubicSlicerNext"
