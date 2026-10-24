<!-- SPDX-FileCopyrightText: 2026 Paul Woisard -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Home screen widgets (first iteration)

Bosun's **Widgets** tab edits the 720 × 384 home screen. The clock and date are
shown by default, using the system locale. Other cards are opt-in. Select a
card, show/hide it, drag it in the preview, or change X, Y, width and height.
Edits stay in the Bosun preview until **Apply** is clicked; only then are they
saved and sent to the daemon. Quitting Bosun without applying discards the draft (hiding the window keeps it).
Geometry is constrained to the screen. **Text size** adjusts each card’s font
(12–64 px); if it cannot fit, enlarge the card as well. Small cards now use
larger, bold values and tighter padding. Existing configurations gain the new
default text size without moving their cards. Cards may overlap; later cards cover
earlier ones. Restore default layout leaves the weather location intact.
The preview uses the same pure QML overlay as the daemon. A wallpaper is still
selected in the Wallpapers tab; the editor uses a neutral background.

Settings are saved atomically in
`QStandardPaths::GenericConfigLocation/galleoncore/widgets.json`, shared by Bosun
and galleond on Linux and Windows. The daemon picks up edits within two seconds.
Outside game mode, the existing media screen, screensaver and wallpaper/theme
selection overlays keep priority. Game mode takes priority over media and
inhibits the module screensaver, as described below. Disabled cards do not trigger periodic screen redraws.

| Card | Linux | Windows |
| --- | --- | --- |
| Clock/date | System locale | System locale |
| Weather | Open-Meteo over HTTPS | Open-Meteo over HTTPS |
| CPU usage | `/proc/stat` deltas | `GetSystemTimes` deltas |
| RAM usage | `/proc/meminfo`, available memory | `GlobalMemoryStatusEx` |
| CPU temperature | Intel coretemp / AMD k10temp or zenpower via hwmon, first readable channel | Unavailable pending a sensor provider |
| Game FPS | Live MangoHud CSV | PresentMon shared-service API; optional manual CSV |
| Downloads | Counts `.part` / `.crdownload` in the system Downloads folder | Same |

CPU/RAM/temperature/download data refresh every two seconds. Missing values say
Unavailable. Download counts are **partial files**, not an exact active-download
count: paused and abandoned files are included. Firefox and Chromium-family
browsers (Chrome, Chromium, Vivaldi, Edge, Brave, etc.) commonly use these suffixes;
this does not identify the browser. Custom directories and exact progress are
not supported in this iteration. No browser history database is opened.

Weather is disabled by default. The optional City name field replaces the card’s
Weather heading; an empty name restores that heading. This is a display label,
not a city search: coordinates still determine the forecast. The label is saved
locally and shared between Bosun and the daemon. Enter latitude and longitude (decimal point),
then enable the card. Coordinates are sent only to `api.open-meteo.com`; there
is no automatic geolocation. Requests are asynchronous, have a ten-second timeout
and are retried/refreshed every fifteen minutes. On failure, the card reports
unavailability instead of retaining an old temperature. Location is stored
locally in the settings file. Data attribution: [Open-Meteo](https://open-meteo.com/),
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
API: [current temperature and weather codes](https://open-meteo.com/en/docs).

Still to implement: a Windows hardware-sensor provider, GPU/other sensor cards,
network/disk activity and per-core graphs, and browser integration for exact
progress. Windows code and QML are shared/native as described above, but this
iteration has only been compiled and run on Linux; native Windows and physical
keyboard validation remain required.

Validation: `test_homewidgets` covers geometry validation, atomic persistence,
reload between instances, reset, malformed geometry, invalid locations, default
inactivity and card rendering. `test_bosun` checks the new tab and preview. The
existing screen-renderer/controller and media/mode flow suites check regressions.
`test_fpsreader` covers both CSV formats, process/swapchain selection, incomplete
records, stale and invalid values, and large logs. Font sizes and FPS settings
also have persistence/apply-boundary tests; card rendering was visually checked.

## Game FPS setup

The FPS card measures the game via an external capture tool, not the keyboard
renderer or monitor refresh rate. Bosun does not install tools, inject into games,
or start capture automatically. Enable the card, enter an absolute CSV path or
a dedicated log folder, then **Apply**. The card reads fresh complete samples
every two seconds. After ten seconds without log updates it says Waiting for game
capture. Logs are read locally, with bounded 16 KiB header / 64 KiB tail reads.
A folder selects its newest CSV, excluding MangoHud `_summary.csv` files; use a
dedicated folder for this purpose.

### Linux — MangoHud

For launcher checkboxes, shared configuration and Steam Vulkan alternatives to
per-game options, see the [detailed French guide](MODE_JEU_LINUX.fr.md).

Install MangoHud using your distribution's package manager and create a dedicated
log folder, for example `/tmp/galleon-fps`. For Steam, adapt the following launch
option (the folder must already exist):

```sh
MANGOHUD_CONFIG=autostart_log=1,log_interval=500,output_folder=/tmp/galleon-fps mangohud %command%
```

Set Bosun's FPS source to `/tmp/galleon-fps`. For non-Steam games replace
`%command%` with the game's command. Logging must be active; existing MangoHud
users can instead set `output_folder` / `log_interval` in their configuration
and toggle logging with the configured shortcut. Data is taken from the latest
complete `fps` sample. The executable filter is unused for MangoHud CSVs, whose
logs are per game. Capture logs grow while logging remains active.

Sources: [MangoHud configuration](https://github.com/flightlessmango/MangoHud/blob/master/data/MangoHud.conf),
[live CSV writer](https://github.com/flightlessmango/MangoHud/blob/master/src/logging.cpp).

### Windows — PresentMon shared service and direct API

Install the official [PresentMon MSI](https://github.com/GameTechDev/PresentMon/releases/latest).
The console and GUI do not need to be launched. Galleon queries Windows Service
Control Manager for **PresentMonSharedService**, obtains its configured executable
path and checks that **PresentMonService.exe** and **PresentMonAPI2.dll** exist in
that installation directory. The service configuration is authoritative; paths
from an unrelated console or GUI installation are not used for this capture.

Start a Steam game with **Automatic** enabled in Bosun. The daemon detects the
running game and starts its isolated `galleon-presentmon-probe` helper. This loads
the installed API DLL, opens a session, tracks the game PID and polls the
`PM_METRIC_PRESENTED_FPS` average over one second. When several swapchains exist,
the highest valid FPS average is selected. No PresentMon GUI, console command or
external CSV configuration is needed. The bounded internal cache transfers the
API samples to the existing renderer; it is not a user-managed capture log.

The button has three states:

- **Installer PresentMon**: the service installation or required files are absent.
- **Démarrer la capture FPS**: installed, capture inactive. A click enables/retries
  capture for the detected game without applying unrelated pending edits.
- **Arrêter la capture FPS**: the Galleon capture helper is running. Stopping
  closes only Galleon's API session, not the shared Windows service or other
  clients. Automatic restart is suppressed for that game until the user starts
  capture again, or a new game is detected.

An installed but stopped service is started through Service Control Manager if
Windows permits it. Access denied, missing DLL, incompatible API version, session
or query errors appear in Bosun. No silent elevation, group membership change or
graphics-driver disabling is performed. If service startup is denied, start it
through Windows Services with appropriate authorization or repair the MSI.
The SDK API major version must match the vendored public v2.6.0 API header (major
3); DLL symbols and returned buffer layouts are checked before reading FPS.

Game detection prefers the foreground Steam process, otherwise retains the
current game. For non-Steam games, add the executable name once in **Game artwork
override**, then Apply; title and image are optional. Desktop/browser processes
are excluded. Protected processes may remain inaccessible. Advanced manual CSV
mode remains available as a compatibility option; Linux MangoHud is unchanged.

The service-backed FPS path still needs physical Windows game validation. Native
CI covers the adapter using a fake API DLL, file checks, error handling, UI states,
and installed program lifecycle. Shared regression tests run on Linux as well.

Sources: [public API](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/IntelPresentMon/PresentMonAPI2/PresentMonAPI.h),
[official service MSI layout](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/IntelPresentMon/PMInstallerLib/Library.wxs).

## Automatic game screen (first iteration)

Bosun has separate **Widgets** and **Gaming** tabs. Widgets edits only the
home cards; Gaming combines recorder controls with the automatic activation switch, game preview,
FPS source, optional **Local sensors**, and per-game artwork associations. Each page has an **Apply** button to save the shared settings draft. The FPS
source is shared by game mode and the optional home FPS card.
A valid fresh capture activates the dedicated game screen even when the home
FPS card is hidden. It shows a game image/title, large FPS and CPU/GPU temperatures.
After ten seconds without fresh capture data it returns to the home/media screen.
A stopped capture also ends the mode even if the game process is still running:
this iteration detects capture activity, not every game independently of capture.

While this mode is active, the keyboard's animated screensaver is inhibited.
An already running screensaver wakes immediately. Leaving game mode restarts the
full configured idle period; the saved screensaver settings are preserved. This
controls the Galleon module's screensaver, not the operating system's sleep policy.
Wallpaper/theme selection temporarily takes precedence over the game view.

For Steam, the captured executable is matched to a running process. Linux uses
its SteamAppId environment value; Windows matches its executable path to local
Steam manifests. Local Steam artwork is preferred in this order: landscape hero,
header, then existing cover fallback. No game image is downloaded. Identification
can fail if a process is inaccessible, has already exited, or runs inside an
isolated container. Artwork is resolved when the captured executable changes.
No artwork: the game screen uses a dark background and still shows its metrics.

For Heroic/Lutris or a missing Steam image, enter the captured executable name
(shown in Capture active), an optional display title and an absolute local image
path under **Game artwork override**. This association is saved per executable.
Use a landscape image; files over 32 MB / 32 million pixels are rejected. This
iteration's game layout is fixed; home card geometry remains independent.

Temperature sources:

- MangoHud CSV `cpu_temp` / `gpu_temp` columns, when valid and nonzero. Add
  `cpu_temp,gpu_temp` to the MangoHud configuration to enable their collection.
- Linux CPU: existing hwmon provider. AMD GPU: amdgpu hwmon fallback.
- NVIDIA GPU on Linux/Windows: asynchronous, read-only `nvidia-smi` query when
  available, with a two-second timeout. It uses the first listed GPU.
- Optional Windows CPU/GPU: run LibreHardwareMonitor with its web server bound
  to `127.0.0.1:8085`, then enable **Local sensors** in Bosun. Only
  `http://127.0.0.1:8085/data.json` is read; redirects are not followed, responses
  are limited to 1 MiB, and requests time out after two seconds. No sensor control
  or writes are performed. It selects the highest valid CPU/GPU temperature
  channel; Fahrenheit values are converted. Missing/failed providers show `—`.

The LibreHardwareMonitor option requires its local server to be running; an
administrator/elevated daemon is not required by Galleon itself. Sensor access
permissions depend on the provider. Authentication-enabled local servers are
not supported yet. Native Windows CI covers compilation, process detection with a fixture in a
Steam library, capture lifecycle, runtime-state freshness and installed app
launching. Real-game FPS capture, UAC authorization and physical keyboard
validation still need testing on the owner’s Windows PC. Shared regression tests
also run on Linux.

Provider reference: [LibreHardwareMonitor HTTP server](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitor.Windows.Forms/Utilities/HttpServer.cs).

### CPU/GPU car-style gauges

The game screen now shows CPU and GPU utilization on graduated 0–100% dials,
with a needle, a central percentage and the temperature below. FPS remains large
on the left. Colored segments turn amber from 70% load and red from 90%; these
colors describe utilization, not thermal alarm limits. Missing utilization is
shown as `—` with no needle, independently of temperature availability.
The dials use pure Qt Quick geometry for deterministic offscreen software
rendering on Linux and Windows, with no GPU rendering dependency.

Sources (first valid available): MangoHud `cpu_load` / `gpu_load` columns; native
Linux/Windows aggregate CPU counters; the local LibreHardwareMonitor provider's
CPU total and GPU core load sensors (`/load/0`); NVIDIA `utilization.gpu`; Linux
AMDGPU `gpu_busy_percent`. As for temperatures, NVIDIA uses the first listed GPU;
LibreHardwareMonitor picks the highest matching value if multiple devices exist.
Loads outside 0–100 or non-finite samples are rejected, rather than displayed as
zero. MangoHud utilization and native counters represent aggregate device load,
not CPU/GPU time attributable exclusively to the captured game process.

Some MangoHud captures report zero for every GPU metric when GPU telemetry is
unavailable (including CS2 under the Steam runtime). A zero GPU load accompanied
by at least two zero temperature, core-clock or VRAM readings is treated as
missing telemetry, allowing the native GPU provider to supply the gauge. A zero
load with valid nonzero GPU telemetry remains a genuine idle reading.

NVIDIA provider reference: [query fields](https://nvidia.custhelp.com/app/answers/detail/a_id/3751/~/useful-nvidia-smi-queries).

### Native GPU telemetry

`galleon-gpu-probe` loads the installed driver libraries dynamically. NVIDIA uses
NVML on Linux and Windows (`libnvidia-ml.so.1`/`.so`, or the system/NVSMI
`nvml.dll`). Linux AMD uses AMD-SMI (`libamd_smi.so`, optionally under
`/opt/rocm/lib` or `lib64`) when installed and supported. AMD-SMI is not guaranteed
to be installed with every consumer AMD graphics driver. Native valid GPU load
and temperature take priority over capture-log GPU fields. Unsupported values
and sentinels are rejected; a genuine 0% is kept. The first readable GPU is used.

Driver calls run outside the daemon with a two-second deadline. If native APIs
are unavailable, `nvidia-smi`, capture fields, Linux amdgpu hwmon and the optional
local LibreHardwareMonitor provider remain fallbacks. Native probing retries
after 60 seconds following a failure. AMD-SMI types are pinned to the MIT-licensed
ROCm 7.2.4 ABI; the proprietary libraries themselves are never bundled.

ADLX for AMD Windows is not integrated: the consulted SDK v1.5 licensing restricts
source redistribution and Free Software License combinations. No ADLX SDK header
or binary is copied into this GPL project. AMD Windows keeps its existing local
sensor fallback pending a compatible integration route.

References: [NVML](https://docs.nvidia.com/deploy/nvml-api/),
[AMD-SMI API](https://github.com/ROCm/amdsmi/blob/rocm-7.2.4/include/amd_smi/amdsmi.h),
[ADLX SDK terms](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/v1.5/ADLX%20SDK%20License%20Agreement.pdf).
