#!/bin/bash


# Get project root using git
PROJECT_ROOT="$(git rev-parse --show-toplevel)"
INSTALL_DIR="$PROJECT_ROOT/app"
LIB_DIR="$INSTALL_DIR/lib"

# Add the library directory to LD_LIBRARY_PATH so the system knows where to find the libraries
export LD_LIBRARY_PATH="$LIB_DIR:$LD_LIBRARY_PATH"

# Fix blank Workbench tab: app omits webkit_uri_scheme_response_set_content_type()
# for pages:// URIs, causing WebKit to abort with "Frame load interrupted" (issue #64)
WORKBENCH_FIX="$PROJECT_ROOT/workbench-fix/acnext-workbench-fix.so"
if [[ -f "$WORKBENCH_FIX" ]]; then
    export LD_PRELOAD="$WORKBENCH_FIX${LD_PRELOAD:+:$LD_PRELOAD}"
fi

# Run the Anycubic Slicer executable
"$INSTALL_DIR/bin/AnycubicSlicerNext"
