# GalleonCore 0.2.0

## Home widgets

Bosun’s Widgets tab edits visibility, position, size and font size with an explicit
Apply action. Available widgets include localized clock/date, weather with city
name, CPU/RAM, available CPU temperature sensors, partial download files and FPS.

## Gaming

A fresh game capture activates the game screen, local artwork, FPS and CPU/GPU
gauges, while keeping the module screensaver off. Gaming keys appear only while
a game is detected and are removed again when the game ends, including after
switching key modes. Missing measurements are shown as unavailable.

- Windows detects the installed PresentMon shared service and API. No PresentMon
  GUI needs to be opened for capture. The button distinguishes ready/waiting,
  starting, running and retry states.
- Linux uses MangoHud CSV logs. Bosun creates the shared configuration and log
  directory while preserving unrelated options. Setup instructions are separated
  into Common, Steam, Heroic, Lutris, PlayOnLinux and Bottles sections.
- NVIDIA sensors use NVML; supported Linux AMD installations use AMD-SMI or hwmon.
  AMD Windows still requires a compatible local sensor provider; ADLX is not bundled.

## Recording controls

The unified Gaming tab remembers Automatic, Game Bar or OBS. Automatic selects
Game Bar on Windows and OBS on Linux. Unavailable choices are disabled.
OBS uses its local WebSocket API and confirmed states. Game Bar uses shortcuts
without claiming an unconfirmed recording state. Pad controls use built-in icons.

## Validation and limits

The owner confirmed widgets and FPS measurement on Windows, and CS2 with Steam
on Linux. Other launchers, DirectX translation paths and recording integrations
have their own documented limitations; these confirmations do not constitute
validation of every game or GPU. Linux game detection expires after roughly ten
seconds without fresh CSV data. Flatpak needs compatible MangoHud extensions and
folder permissions.

Release checks cover GCC, Clang with ASan/UBSan, fuzzing, static analysis, QML,
licensing, native Windows tests and install/upgrade/uninstall tests, plus Linux
package builds. The shared NVML test ABI fixes the earlier sanitizer failure.

See [Linux launcher setup](MODE_JEU_LINUX.fr.md), [widgets and sensors](HOME_WIDGETS.md)
and [recording configuration](GAMING_RECORDING.md) for setup and remaining limits.
