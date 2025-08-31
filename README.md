

## Getting Started


To clone this project from GitHub via HTTPS into the `~/opt/AnycubicSlicer` directory, run:

```bash
git clone https://github.com/slavaz/AnycubicSlicerRunner.git ~/opt/AnycubicSlicer
```

> **Note:** This project is intended to be used until Anycubic officially releases the slicer via Snap or Flatpak. Once an official Snap or Flatpak package is available, this project may become obsolete.

# AnycubicSlicerRunner

AnycubicSlicerRunner is a set of Bash scripts for managing and updating Anycubic Slicer software (https://wiki.anycubic.com/en/software-and-app/anycubic-slicer-next-linux). It provides scripts and tools for checking updates, backing up application data, and handling package installations.

## Project Structure

- `AnycubicSlicer.sh` - Main launcher script
- `check-update.sh` - Checks for new versions and downloads updates
- `updater.sh` - Handles backup and update installation


## Usage


### Updating the Application
Run the updater script:

```bash
bash updater.sh
```
This will backup the current application files and install the latest downloaded version.

### Automated Weekly Updates with Cron
You can automate updates by running `updater.sh` once per week using cron. For example, if the project is installed in `~/opt/AnycubicSlicer`, add the following line to your crontab (edit with `crontab -e`):

```
0 3 * * 0 bash ~/opt/AnycubicSlicer/updater.sh
```
This will run the updater every Sunday at 3:00 AM.


## Tested Distributions

This project has been tested on the following Linux distribution:

- **Fedora release 42 (Adams)**

## Requirements

- Linux OS
- Bash shell
- git (used to detect the project root)
- curl (for downloading updates)
- ar (for extracting .deb packages)
- tar (for unpacking data from .deb packages)


## License
See `LICENSE` for license information.


## Support
For issues or questions with AnycubicSlicer app, please contact Anycubic support or refer to the official documentation.


## Trademark and Icon Notice
The file `anycubic-slicer-icon.png` is a screenshot of the official Anycubic logo, used here for personal/non-commercial purposes. The logo is a trademark of Anycubic. If you are distributing this project, please ensure you comply with Anycubic's trademark and copyright policies.

## Disclaimer
This project is provided "as is" without any warranty. Use at your own risk. The authors and contributors are not responsible for any damage, data loss, or other issues resulting from the use of this software.
