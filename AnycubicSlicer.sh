#!/bin/bash

set -euo pipefail

# Get project root using git (fallback to script directory)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR")"
INSTALL_DIR="$PROJECT_ROOT/app"
LIB_DIR="$INSTALL_DIR/lib"

# Add the library directory to LD_LIBRARY_PATH so the system knows where to find the libraries
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Ensure TMP/TEMP point to existing directories so Boost filesystem doesn't throw filesystem_error
if [[ -n "${TMP:-}" && ! -d "$TMP" ]]; then
    mkdir -p "$TMP" 2>/dev/null || unset TMP
fi
if [[ -n "${TEMP:-}" && ! -d "$TEMP" ]]; then
    mkdir -p "$TEMP" 2>/dev/null || unset TEMP
fi
if [[ -n "${TMPDIR:-}" && ! -d "$TMPDIR" ]]; then
    mkdir -p "$TMPDIR" 2>/dev/null || unset TMPDIR
fi

# Run under XWayland (X11) because wxWidgets/EGL 3D viewport requires X11 windowing
export GDK_BACKEND=x11

# Disable WebKit2GTK bubblewrap sandbox (bwrap fails with "Load request cancelled"
# when /usr/share/AnycubicSlicerNext is a symlink outside /usr/share)
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1

# In WebKit2GTK 2.54.2 (Fedora 44), legacy non-composited BackingStoreCairo was removed;
# setting WEBKIT_DISABLE_DMABUF_RENDERER=1 sets s_rendererBufferTransportMode=0 (None),
# which prevents UIProcess from creating AcceleratedBackingStore (leaving views blank).
# Instead, keep AcceleratedBackingStore enabled in SharedMemory (SHM) mode while disabling
# GPU compositing so WebKitWebProcess renders via CPU/SHM without touching PlatformDisplay/GBM.
unset WEBKIT_DISABLE_DMABUF_RENDERER
export WEBKIT_DMABUF_RENDERER_FORCE_SHM=1
export WEBKIT_DISABLE_COMPOSITING_MODE=1
unset WEBKIT_DMABUF_RENDERER_DISABLE_GBM
unset WEBKIT_SKIA_ENABLE_CPU_RENDERING

# Build and preload the Fedora/WebKit2GTK compatibility fix shim:
# 1) Fontconfig infinite loop workaround for Skia in WebKit2GTK (FcPatternGetString).
# 2) Blocks runtime setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1") so SHM backing store works.
# 3) Forces WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER (2) and allows file/CORS access.
# 4) Clamps negative dimensions < -1 passed to gtk_widget_set_size_request().
# 5) Injects missing MIME types for pages:// URIs on Workbench tab (issue #64).
# 6) Re-issues target URI if about:blank finishes after target during view initialization.
WORKBENCH_FIX_SRC="$PROJECT_ROOT/workbench-fix/acnext-workbench-fix.c"
WORKBENCH_FIX="$PROJECT_ROOT/workbench-fix/acnext-workbench-fix.so"
if [[ -f "$WORKBENCH_FIX_SRC" ]] && { [[ ! -f "$WORKBENCH_FIX" ]] || [[ "$WORKBENCH_FIX_SRC" -nt "$WORKBENCH_FIX" ]]; }; then
    gcc -shared -fPIC -O2 -o "$WORKBENCH_FIX" "$WORKBENCH_FIX_SRC" -ldl
fi
if [[ -f "$WORKBENCH_FIX" ]]; then
    export LD_PRELOAD="$WORKBENCH_FIX${LD_PRELOAD:+:$LD_PRELOAD}"
fi

# Log output to logs/terminal.log (ACNEXT_FIX_DEBUG=1 enables verbose shim logging)
export ACNEXT_FIX_DEBUG="${ACNEXT_FIX_DEBUG:-0}"
LOG_DIR="$PROJECT_ROOT/logs"
mkdir -p "$LOG_DIR"
if [[ "$ACNEXT_FIX_DEBUG" != "0" ]]; then
    export ACNEXT_FIX_LOG="$LOG_DIR/acnext-fix.log"
    : > "$ACNEXT_FIX_LOG"
fi
exec > >(tee "$LOG_DIR/terminal.log") 2>&1

# Run the Anycubic Slicer executable
exec "$INSTALL_DIR/bin/AnycubicSlicerNext" "$@"
