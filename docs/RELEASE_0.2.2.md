# GalleonCore 0.2.2

## MangoHud CSV access for Flatpak launchers

Bosun's **Gaming → Linux · Préparer les FPS** page now detects installed Flatpak
game launchers (Heroic, Bottles, Lutris and Steam). Each installed launcher gets
an explicit **Autoriser les CSV** action.

The action applies per-user, per-application Flatpak permissions: read-only
access to the shared MangoHud configuration directory, read/write access to
GalleonCore's FPS CSV folder, and an explicit `MANGOHUD_CONFIGFILE` path. It
does not grant access to the whole home directory or change other launchers.

Previously configured MangoHud options are retained. Users should first run
**Configurer MangoHud pour Galleon** and then grant access to the relevant
Flatpak launchers. The Flatpak MangoHud runtime extension and in-game/launcher
MangoHud activation remain necessary; quit and restart the launcher after
changing permissions.

## Validation before publication

- Run full CI: GCC, Clang + ASan/UBSan, static analysis and QML lint, Linux
  distribution packaging, AppImage and Windows builds.
- Confirm the button against actual installed Flatpak launchers and verify a
  continuously updated MangoHud CSV is visible to GalleonCore during gameplay.
- Verify return from Gaming to the home screen after CSV data stops.
- Do not publish `v0.2.2` until these checks pass.

See [the Linux gaming guide](MODE_JEU_LINUX.fr.md) for setup, manual overrides
and known compatibility limits.
