# Desk lights: driving the mouse (research and desk-test list)

Written 2026-10-25 without touching hardware. Everything marked **unverified** needs a desk test.

## What the helper does (`lab/tools/DeskLights/Mouse.cs`)

- Only acts when a scene has a non-null `mouse`. While `mouse` is `null` (or the plugin is quiet and the neutral scene is showing) it enumerates nothing and opens nothing; status `mouse` is `idle`.
- When a scene asks for the mouse: start a `DeviceWatcher` on `LampArray.GetDeviceSelector()`, open each LampArray with `FromIdAsync`, keep only `HardwareProductId == 16537`, and let every other lamp array go untouched. Then `SetColor` on all 8 lamps (repainted only when the color changes).
- When the scene stops asking: set `IsEnabled = false` on that LampArray, stop the watcher, drop the reference. Nothing is painted black on the way out, because black would defeat the aim of letting the mouse's own effect resume.
- On exit the same release runs.

## What the docs say

- `LampArray` remarks: an app can set lamp state at any time, but the system applies it **only while the app is in focus**. A packaged app that is the user's *background light controller* (Settings > Personalization > Dynamic lighting > Background light control) is also allowed when not focused. The dials already work this way.
- `IsAvailable` says whether this process currently has control; `AvailabilityChanged` fires when it changes, including when the user flips access in Dynamic Lighting settings.
- `IsEnabled`: "When disabled, state for a lamp index can be set internally, but will not be sent to the device. When set back to enabled, modified state is flushed to the device. By default, LampArray is always enabled." So disabling stops *our* writes. The docs do not say it returns the device to its own effect.
- Several background apps competing for one device: the one highest in the Background light control list wins. Foreground apps beat background ones. Background settings are tied to the device and the USB port.

## What the docs do not say (the open questions)

1. Does merely opening a LampArray with `FromIdAsync` take the mouse away from G HUB's Screen Sampler? **Unverified.** Windows' Dynamic Lighting service is the party that talks to the device (the HID LampArray "autonomous mode" attribute is how a device is told to run its own effect or follow the host). It plausibly flips autonomous mode off only when a controller actually sets colors, and back on when no app holds the device, but this is the HID spec's design, not a documented Windows guarantee for packaged apps.
2. Does the mouse already follow Windows' own Dynamic Lighting (the master "Use Dynamic Lighting on my devices" switch and its effect) rather than G HUB? Then G HUB isn't the thing to compete with. Check the per-device toggle for the mouse in Settings.
3. Does dropping the reference (or setting `IsEnabled = false`) hand the mouse back to its own effect? `LampArray` is not `IDisposable`/closable in the projection, so the reference is released by the runtime, possibly only at the next GC. **Unverified.** Fallback if it lingers: exit and restart the helper (releases everything), or turn the mouse's Dynamic Lighting toggle off and on.
4. The dials share the same background-controller slot, so the mouse should follow the same rule; but the ordering against G HUB (if it is in the list) is unknown.

## Why this approach

Of the options (open at startup and set a color; open only on demand; never open), "open only on demand, release when not needed" is the least invasive that still lets the plugin drive the mouse: with `mouse: null` the helper behaves exactly like v1, so the mouse is never touched unless the plugin chooses. It also keeps the other lamp arrays (keyboard PID 11020, dials PID 11032 via `Dials`) out of this class.

## Desk test (owner, in order, stop at the first surprise)

Run with the keyboard and dials already working; the mouse test is separate.
1. With no `mouse` in the scene: confirm G HUB's Screen Sampler keeps running on the mouse and status says `"mouse":"idle"`. (Expected: unchanged from v1.)
2. Send a scene with `mouse` steady red. Expect status `ours`, mouse red. If `not-ours`: check Background light control and the mouse's Dynamic Lighting toggle. If `absent`: PID may differ; look at the log line `mouse: found`.
3. Send `mouse: null` again. **Does Screen Sampler resume, and how long does it take?** If it stays red/dark, record that: then handing back needs a helper restart, and the plugin should avoid toggling `mouse` on and off casually.
4. Does G HUB fight back (colors flicker between ours and Screen Sampler) while we hold it? Watch the log for repeated `control taken` / `control granted` lines.
5. Blink: send a scene with `mouse: blink` and confirm it flashes for about 0.6 s and goes dark (not back to Screen Sampler; "dark" is what `blink` means while driving, and `mouse: null` is how to hand back).
6. Unplug and replug the mouse while driving: the helper should re-find it (watcher stays running only while driven).

## Known limits

- A `mouse` blink or `off` leaves the mouse dark, not on its own effect. Only `null` releases.
- No brightness/battery logic here; the plugin owns the rules.
- Nothing here has run against a real device.
