# Gaming recording controls

Bosun's unified **Gaming** tab contains automatic game-mode/FPS settings and recording controls. Recording is independent of the FPS provider:

| Preference | Windows | Linux |
|---|---|---|
| Automatic | Game Bar | OBS |
| Game Bar | Enabled when the current user's Game Bar package is detected | Disabled |
| OBS | Enabled when OBS is installed or its local WebSocket server responds | Same |

Automatic follows the platform default; it does not silently switch software if
that default is missing. An unavailable saved choice remains saved. Installed
software and a working capture session are different: OBS can be selected while
stopped, but its pad commands remain disabled until its WebSocket connection is
identified and the relevant capabilities/status have arrived.

## The pad

The Gaming key page exists only while Galleon detects an active game. It is
selected on game-mode entry and removed from the dial's choices on game-mode
exit, including when the user selected another key page during the game. If
Gaming is still displayed at exit, the previous page is restored; otherwise the
current ordinary page stays selected. Opening Bosun does not activate this mode.
The preview/buttons in Bosun are visible only during an active game. The **Show
on pad** button can return to Gaming during that game; it cannot create a Gaming
page when no game is active. Built-in vector icons are shared with the pad and
do not need a desktop icon theme. The daemon adds this temporary runtime theme;
it does not replace the game launcher page or rewrite the user's pages JSON.
Bosun shows a preview of the same twelve positions. Four positions are used;
the remaining eight are blank. Unavailable actions are dimmed and ignored.

| Position (row-major) | Game Bar | OBS |
|---|---|---|
| 1 | Start/stop shortcut, Win+Alt+R | Toggle recording |
| 2 | Save recent gameplay, Win+Alt+G | Pause/resume recording |
| 3 | Screenshot, Win+Alt+Print Screen | Save replay buffer |
| 4 | Open Game Bar, Win+G | Start/stop replay buffer |

Game Bar uses the documented default shortcuts. Keep those defaults in Game Bar.
Game/app support, Windows capture settings, background capture for recent
moments, and OS permissions still determine whether a shortcut takes effect.
Galleon reports **shortcut sent; recording state not confirmed**. It never
lights a recording/microphone state based on sending a shortcut. Game Bar has
no pause command in this integration. A failed input send is reported.

OBS controls use WebSocket requests, not keyboard shortcuts. Galleon checks the
server's `availableRequests`; pause requires a confirmed active recording, and
saving a replay requires a confirmed active replay buffer. Configure the replay
buffer and recording output in OBS itself. A request may still fail because the
chosen output format/settings do not support the operation; OBS errors appear
in Bosun. A command response alone never changes the displayed recording state:
Galleon reads `GetRecordStatus` and `GetReplayBufferStatus`, refreshes after
output events, and polls every three seconds. Disconnects/timeouts clear all
confirmed states and disable the commands. Existing recordings are not stopped
when Bosun/Galleon exits or when the software preference changes.

## Connect OBS

Use OBS with obs-websocket 5.x (built into OBS 28 and newer). In OBS, open
**Tools → WebSocket Server Settings**, enable the server and note its port and
password. In Bosun → Gaming, choose OBS (or Automatic on Linux), enter the port
(default **4455**) and password, then save. An empty password field preserves
the previous password; use **Clear password** to remove it.

Only `ws://127.0.0.1:<port>` is used. Authentication implements the protocol's
salt/challenge SHA-256 exchange. The password is not displayed or logged. It is
saved locally, without encryption, in `galleoncore/recording.ini` under Qt's
per-user generic configuration directory; Linux creates the file with owner
read/write permissions. Treat the file as a credential. Bosun and the daemon
read the same preferences; changes propagate on the next three-second refresh.
No commands start OBS, change its scenes/sources or configure its capture output.

Discovery checks PATH, OBS's usual Windows installation directories and uninstall
registry entries, and Linux system/per-user Flatpak installations. A responding
local WebSocket server also establishes availability for custom installations.
Game Bar detection checks the current user's registered XboxGamingOverlay package;
it does not assume that every Windows installation includes Game Bar.

## Validation

Automated tests use a local fake WebSocket server and a fake shortcut sender:
platform defaults, preference persistence, unavailable software, authentication,
request discovery, disabled actions, no speculative state, disconnect handling,
and routing from simulated physical pad reports. Bosun's Gaming page is rendered
in the existing UI smoke test. These checks do not certify a real Windows
Game Bar capture or every OBS recording format; physical Linux/Windows tests
with the user's installed applications remain necessary.

References: [OBS WebSocket 5 protocol](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md),
[OBS WebSocket installation](https://github.com/obsproject/obs-websocket),
[Microsoft Game Bar shortcut catalog](https://learn.microsoft.com/en-us/windows/configuration/windows-backup/catalog).
