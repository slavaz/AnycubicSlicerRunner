# AnycubicSlicerRunner

AnycubicSlicerRunner is a set of Bash scripts and a runtime compatibility shim for running and maintaining [Anycubic Slicer Next for Linux](https://wiki.anycubic.com/en/software-and-app/anycubic-slicer-next-linux) on modern Linux distributions (such as Fedora 42–44+).

It automates downloading official `.deb` updates, unpacks and configures the environment, and transparently resolves upstream compatibility bugs with WebKit2GTK, Fontconfig, and GTK3.

> **Note:** This project provides a non-invasive compatibility layer and update automation until Anycubic officially packages the slicer for modern distributions (e.g. via Flatpak or Snap).

---

## Getting Started

### Installation

Clone the repository into `~/opt/AnycubicSlicer`:

```bash
git clone https://github.com/slavaz/AnycubicSlicerRunner.git ~/opt/AnycubicSlicer
cd ~/opt/AnycubicSlicer
```

Install the latest version of the slicer by running the updater:

```bash
bash helpers/updater.sh
```

Launch the application:

```bash
./AnycubicSlicer.sh
```

### Automated Weekly Updates with Cron

To keep the slicer automatically updated, add a cron job (edit with `crontab -e`):

```bash
0 3 * * 0 bash ~/opt/AnycubicSlicer/helpers/updater.sh
```

This runs the updater every Sunday at 3:00 AM, backing up the current installation before extracting new packages.

---

## Linux Compatibility Layer & Workarounds

Anycubic Slicer Next is compiled against an older Ubuntu environment. On modern Linux distributions (such as Fedora 42/43/44 with WebKit2GTK 4.1 2.42+), multiple breaking incompatibilities occur out of the box. 

This repository provides an automated `LD_PRELOAD` shim ([`workbench-fix/acnext-workbench-fix.c`](workbench-fix/acnext-workbench-fix.c)) compiled automatically on launch by [`AnycubicSlicer.sh`](AnycubicSlicer.sh) or manually via [`workbench-fix/Makefile`](workbench-fix/Makefile), resolving the following issues without modifying proprietary binaries:

### Building the Compatibility Shim

The shim is built automatically when you launch `./AnycubicSlicer.sh` if it hasn't been built yet or if the source file was updated.

To build, clean, or debug manually:

```bash
# Build the shared library (acnext-workbench-fix.so)
make -C workbench-fix

# Clean built artifacts
make -C workbench-fix clean

# Debug build with symbols (-g -O0)
make -C workbench-fix DEBUG=1
```

> **Note:** No WebKit2GTK or GTK development packages (`-devel`) are required. The shim only requires a standard C compiler (`gcc` or `clang`), `make`, and `glibc`.

### 1. Fontconfig / Skia Infinite Loop Workaround (Blank WebViews & Process Hang)
* **Symptom:** All embedded WebViews (Home, MakerOnline, Workbench, and the Casdoor login modal) remain completely blank. When closing the main window, the process hangs in the terminal and requires `Ctrl+C`.
* **Root Cause:** In WebKit2GTK 4.1, the Skia graphics engine calls `SkFontMgr_fontconfig::GetFamilyNames()`. When iterating font patterns, Fontconfig returns `FcResultNoMatch (1)` on fonts or cache entries lacking an `FC_FAMILY` property. Because Skia checks only for `FcResultNoId (3)` to terminate the loop and executes `continue` on other results, the loop runs infinitely at 100% CPU, deadlocking `WebKitWebProcess`.
* **Fix:** The shim intercepts `FcPatternGetString()`. If `FcResultNoMatch` is returned during family iteration, it is converted to `FcResultNoId` so Skia terminates the loop cleanly and safely.

### 2. Modern WebKit2GTK Backing Store Preservation (DMABUF & SHM Mode)
* **Symptom:** WebViews fail to create an accelerated backing store and display empty frames.
* **Root Cause:** Anycubic Slicer internally calls `setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1")`. In WebKit2GTK 2.42+, legacy non-composited Cairo backing store was completely removed; disabling DMABUF renderer sets buffer transport mode to None, leaving UIProcess unable to paint web content.
* **Fix:** The shim intercepts `setenv()` to block `WEBKIT_DISABLE_DMABUF_RENDERER`, and forces Shared Memory buffer transport with unaccelerated CPU compositing (`WEBKIT_DMABUF_RENDERER_FORCE_SHM=1`, `WEBKIT_DISABLE_COMPOSITING_MODE=1`).

### 3. Workbench Tab MIME Type Injection (Issue #64)
* **Symptom:** The Workbench SPA tab is blank, failing with WebKit error `Frame load interrupted`.
* **Root Cause:** The application serves Workbench through a custom `pages://` URI scheme but never calls `webkit_uri_scheme_response_set_content_type()`. WebKitGTK guesses `application/x-extension-html`, treats it as unsupported, and aborts the load.
* **Fix:** The shim intercepts `webkit_uri_scheme_request_finish_with_response()` and automatically injects the correct `Content-Type` header (e.g. `text/html`, `application/javascript`, `font/woff2`, etc.) based on the target resource extension.

### 4. Custom URI Scheme Security & CORS Support
* **Symptom:** Embedded JavaScript fails to make cross-origin requests to local APIs (`anycubic://...` or `pages://...`) due to browser access control security errors.
* **Root Cause:** Custom URI schemes in WebKit2GTK are treated as non-CORS and non-secure by default unless explicitly declared.
* **Fix:** The shim intercepts `webkit_web_context_register_uri_scheme()` and registers custom schemes with `WebKitSecurityManager` as both CORS-enabled and secure.

### 5. Casdoor OAuth & `about:blank` Race Condition
* **Symptom:** The login window opens as a white/empty rectangle.
* **Root Cause:** `wxWebView` starts views with `about:blank` before loading target URLs. If `about:blank` finishes loading asynchronously after the OAuth URL was queued, WebKit can remain on the blank page.
* **Fix:** The shim tracks view URLs and re-issues the target OAuth authorization URL if `about:blank` completes after initialization.

### 6. GTK Widget Negative Dimension Clamping
* **Symptom:** Terminal flooded with `Gtk-CRITICAL **: gtk_widget_set_size_request: assertion 'width >= -1' failed`.
* **Root Cause:** Layout calculations in the slicer occasionally pass negative dimensions `< -1` when resizing or collapsing panels.
* **Fix:** The shim intercepts `gtk_widget_set_size_request()` and clamps negative values to `-1`.

### 7. Boost Filesystem Environment Sanitization
* **Symptom:** Slicer crashes immediately on startup with `boost::filesystem::filesystem_error: temp_directory_path: Not a directory`.
* **Root Cause:** Boost's `temp_directory_path()` throws an exception if `TMP` or `TEMP` environment variables point to paths that do not exist on the system.
* **Fix:** `AnycubicSlicer.sh` verifies `TMP`, `TEMP`, and `TMPDIR`, creating or unsetting them if they point to non-existent directories.

---

## Project Structure

```text
AnycubicSlicer/
├── AnycubicSlicer.sh                   # Main launcher script (sets env, builds & preloads shim)
├── helpers/
│   ├── check-update.sh                 # Fetches latest version metadata and downloads .deb packages
│   └── updater.sh                      # Handles backups, extraction, desktop icon setup, and font links
├── workbench-fix/
│   ├── Makefile                        # Build script for the compatibility shim
│   ├── acnext-workbench-fix.c          # C source for the LD_PRELOAD compatibility shim
│   └── acnext-workbench-fix.so         # Auto-compiled shared library (excluded from git)
├── deb-packages/                       # Downloaded .deb packages (cached)
├── backups/                            # Dated application backups created before updates
└── app/                                # Installed Anycubic Slicer application files
```

---

## Tested Distributions

- **Fedora 42** (x86_64)
- **Fedora 43** (x86_64)
- **Fedora 44** (x86_64)

## Requirements

- Linux (x86_64)
- Bash 4+
- `gcc` or `clang`, and `make` (to build the compatibility shim)
- `git`
- `curl`
- `ar` (from `binutils`) and `tar` (for `.deb` package extraction)

---

## License

See `LICENSE` for license information.

## Trademark and Icon Notice

The file `anycubic-slicer-icon.png` is an official Anycubic logo used for personal/non-commercial desktop integration. The logo is a trademark of Anycubic.

## Disclaimer

This project is provided "as is" without warranty of any kind. The authors and contributors are not affiliated with Anycubic. Use at your own risk.
