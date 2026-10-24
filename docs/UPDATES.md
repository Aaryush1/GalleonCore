<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Bosun update checks (next release)

This feature is being prepared for the next release; it is not in v0.2.0.

For Windows Setup/portable installations and Linux AppImages, **Updates** in
Bosun's bottom bar opens the installed version, latest stable version and last
successful check. **Check now** checks GitHub manually. Automatic checking is
on by default, starts shortly after Bosun opens, and attempts at most once per
24 hours while Bosun runs. The switch persists between sessions. Closing Bosun
stops checks; minimizing to the tray keeps it running. A new version changes the
bottom-bar button without interrupting work with a popup.

**Download** opens the matching release asset in the default browser: Windows
x86-64 Setup or Linux x86-64 AppImage. If a suitable uploaded asset is absent or
the architecture is unknown, the release downloads page opens instead. The
browser handles the download; Bosun never executes an installer. Release notes
and the project website are also accessible from this panel.

For native DEB/RPM builds, Bosun first queries local package metadata, without
network access or administrator privileges. A positive-priority APT repository
index offering `galleoncore`, or an available DNF repository package, suppresses
GitHub checks and the update button. This covers PPA/COPR installations.
A standalone DEB/RPM with no such repository retains GitHub update notifications
and a matching distribution-specific download when one exists. The same package
can therefore behave differently depending on the repositories configured.

The probe runs once per Bosun session, with a five-second deadline and 64 KiB
output limit. Restart Bosun after changing repositories. It uses `apt-cache policy
galleoncore` or `dnf --cacheonly repoquery --available --queryformat
'GALLEON_UPDATE_REPO:%{repoid}\n' galleoncore`. If cached metadata is unavailable
or the probe fails, Bosun exposes manual checks but skips automatic checks for
that session, avoiding unsolicited duplicate alerts. Refresh package metadata
with your package manager and restart Bosun to retry. The automatic-check switch
is available for personal preferences. AUR/source builds retain this switch;
Bosun does not infer that an AUR helper is managing upgrades.

Builders set `-DGALLEON_PACKAGE_FORMAT=deb` or `rpm`. Standalone Windows and
AppImage builds leave this setting empty. These markers select the probe, not
whether notifications are disabled.

Only stable GitHub releases are considered, using numeric major/minor/patch
comparison (for example 0.10.0 is newer than 0.9.0). A local development version
newer than the release is not offered a downgrade. An installed prerelease may
upgrade to the stable release with the same version number.

Checks use GitHub's public `/repos/Paullux/GalleonCore/releases/latest` API,
without an account or token. GitHub receives the usual connection information
and a User-Agent containing Bosun's version; no hardware identifiers, game
names or personal settings are sent. The local preference, validated release
cache and check timestamps live in `galleoncore/updates.ini` under Qt's user
configuration directory (`~/.config` on Linux). Screenshot and explicit
`--pages` sessions do not start background checks.

Timeouts, offline connections, malformed responses and GitHub rate limits are
reported in the panel. They never block the UI or discard a previously validated
update. Requests have a 15-second deadline and a 1 MiB response limit. Manual
checks have a minimum 60-second interval; GitHub rate limits can extend it.
Only HTTPS links to assets of the official GalleonCore release are accepted.

## Test without publishing a release

Quit Bosun (including its tray icon on Windows), then run:

```sh
galleon-bosun --simulate-update
```

On Windows, run `galleon-bosun.exe --simulate-update` from the installed program
folder. The update panel opens automatically with a permanent **SIMULATION**
notice. It fetches the real latest GitHub release and compares it with a fictitious
installed version `0.0.0`, while still displaying your actual installed version.
No release or tag is created. The Download button opens the **existing published
asset**, so it may download the version you already have; do not install it just
to test the notification. Network failures still exercise the real error display.
The simulation ignores saved cooldowns and repository suppression, disables
background checks, and never saves update preferences, timestamps or release
cache. Restart Bosun normally to leave test mode.
