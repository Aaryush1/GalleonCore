# GalleonCore 0.2.1

## Update notifications in Bosun

Bosun can check the latest stable GitHub release, compare it with the running
version, and notify you through its bottom bar. The update panel shows both
versions, the last successful check, release notes and the project website.
Automatic checks are optional and limited to once per day; manual checks are
also available.

Download opens the matching published asset in your default browser. If no
matching build exists, Bosun opens the release page instead. Installation is
always your choice; Bosun never executes an installer automatically.

## Respect package repositories

DEB/RPM builds query local APT/DNF metadata before enabling notifications.
When a configured repository supplies GalleonCore (including PPA/COPR), the
package manager handles updates without duplicate Bosun notifications.
Independent DEB/RPM installations retain notifications. If repository detection
fails, manual checks remain available and automatic checks pause for the session.

## Test mode and reliability

Run `galleon-bosun --simulate-update` to test using the real latest release and a
fictitious comparison version of `0.0.0`. The panel clearly marks the simulation;
your actual installed version and update preferences stay unchanged.
The owner confirmed this mode on Windows and Kubuntu Resolute.

Network timeouts, rate limits, invalid responses and offline connections are
handled without blocking Bosun. Downloads are restricted to validated HTTPS
assets of the official GalleonCore repository. Checks need no GitHub account.

See [update checks](UPDATES.md) for configuration, privacy and cache limitations.

Version 0.2.0 and earlier do not have the checker: install 0.2.1 manually first,
or update through your configured package repository.
