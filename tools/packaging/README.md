<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Linux distribution

The application code is shared unchanged by all formats. Every configuration uses
`GALLEON_SOURCE_TREE_PATHS=OFF`. Native packages contain galleond, Galleon Bosun,
libgalleon and headers, QML widgets, 24 wallpapers with provenance, desktop icon,
udev rules, an opt-in systemd user unit and modules-load configuration for uinput.
Screensaver videos, probes, the plasmoid and the OpenRGB plugin are excluded.

## Checks and releases

`.github/workflows/linux-packages.yml` builds and tests on PRs, main and manual
runs, and is reusable by the release workflow:

| Target | Build and verification |
| --- | --- |
| PPA/Debian | Ubuntu 24.04 noble and 26.04 resolute: debhelper build, all ctest tests, install, both `--help`, remove; unsigned source package retained |
| COPR/RPM | Fedora 43, 44, 45 and rawhide: rpmbuild including all ctest tests, binary RPM and SRPM, install, both `--help`, remove |
| AUR | Arch Linux: unprivileged makepkg with check(), install and both `--help`; PKGBUILD and generated .SRCINFO retained |
| AppImage | Ubuntu 24.04 / Qt 6.4 baseline: bundle Qt/QML and both programs, both `--help`, extracted offscreen Bosun rendering |

`.github/workflows/linux-release.yml` runs only on a tag exactly matching
`v<CMake project version>` (manual dispatch must also select that tag). It waits
for the existing GCC, Clang/sanitizers, fuzz and lint CI and all package jobs.
It creates a GitHub release with the tested artifacts and canonical source archive,
then publishes to PPA, COPR and AUR only if `ENABLE_LINUX_PUBLISH=true`.
Until credentials are configured, GitHub artifacts/releases work independently.
No workflow pushes automatic version commits to protected main.

The archive is generated once by `prepare.py` with deterministic ordering,
permissions, ownership and timestamps. Every native build uses those same bytes;
the AUR recipe embeds their SHA-256 and points to the matching GitHub release
asset. PR builds use a local mirror of that archive without bypassing checksums.
`PKGBUILD.in` is a template; the usable `PKGBUILD` is a build/release artifact.
Never publish the template or replace the source digest with `SKIP`.

For native distribution builds warnings remain enabled but are not fatal. Fedora
44's GCC 16 reports array-bounds diagnostics in vector-resize code in protocol
tests; this must not prevent packaging on a newer compiler. The dedicated CI still
uses `GALLEON_WERROR=ON` on the supported Ubuntu baseline. No source change or
warning suppression is applied by the package recipes.

## First-time publication setup

Agreed destinations: `ppa:paulwoisard/galleoncore`,
`paullux/GalleonCore` on COPR, and `galleoncore` on AUR.
They must exist and the CI account must have upload rights before enabling uploads.

1. Create the PPA under the existing [Launchpad account](https://launchpad.net/~paulwoisard/+activate-ppa).
   Name: `galleoncore`; display name: `GalleonCore` (already created). Register the public GPG signing
   key and public SSH upload key on that account. With `ppa-dev-tools` installed
   and Launchpad authentication completed, the CLI equivalent is
   `ppa create --owner-name paulwoisard --displayname GalleonCore galleoncore`. Launchpad builds uploaded signed
   **source** packages; a binary .deb is not a PPA upload.
2. Obtain COPR credentials from [the COPR API page](https://copr.fedorainfracloud.org/api/)
   and save the provided configuration locally to `~/.config/copr` with mode 600.
   The project has been created as `paullux/GalleonCore`. To recreate this setup
   with `copr-cli` on the same account:

   ```sh
   copr-cli create --chroot fedora-43-x86_64 --chroot fedora-44-x86_64 \
     --chroot fedora-45-x86_64 --chroot fedora-rawhide-x86_64 \
     --description 'Corsair Galleon Stream Deck support and Bosun settings' \
     --enable-net off GalleonCore
   ```

   The COPR **External Repositories** field must be empty unless a real RPM
   dependency repository is needed. The GitHub project URL belongs in Homepage;
   it is not a yum/dnf repository. The workflow uploads a complete SRPM.

3. Add the CI public SSH key to the [AUR account](https://aur.archlinux.org/account/).
   The first push of the validated recipe to
   `ssh://aur@aur.archlinux.org/galleoncore.git` creates the AUR package; no separate
   empty project needs to be created on the website. Check name availability first.
4. Add these **repository secrets** to `Paullux/GalleonCore` through GitHub settings
   or `gh secret set NAME < /path/to/file`. Never commit keys or paste them into chat.

   | Secret | Contents |
   | --- | --- |
   | `PPA_GPG_PRIVATE_KEY` | Dedicated GPG secret signing key export, usable noninteractively; public key registered with Launchpad |
   | `PPA_SSH_PRIVATE_KEY` | SSH private upload key; public key registered with Launchpad |
   | `COPR_LOGIN` | COPR API login |
   | `COPR_USERNAME` | COPR account name |
   | `COPR_TOKEN` | COPR API token |
   | `AUR_SSH_PRIVATE_KEY` | SSH private AUR deployment key; public key registered with AUR |

5. Optional GitHub variables `PPA_OWNER`, `PPA_NAME`, `COPR_PROJECT`, `AUR_PACKAGE`
   override the agreed defaults. Once accounts and secrets are ready:

   ```sh
   gh variable set ENABLE_LINUX_PUBLISH --body true --repo Paullux/GalleonCore
   ```

6. Commit the tested packaging through the usual PR process, merge, then tag the
   desired release after updating the CMake version. A tag publishes publicly.
   Do not reuse an upstream version for different source bytes: Launchpad and
   AUR checksums require the original source asset to remain stable.
   PPA re-runs may encounter already-uploaded versions; inspect Launchpad before
   re-running and bump the package/upstream version as appropriate. COPR waits
   for its remote build to finish; a successful upload alone is not a green build.

Public destinations are not considered active just because their recipes exist.
Creating projects requires account authentication; local builds and CI configuration
do not grant upload rights or transfer credentials from another repository.

## Local builds

From the repository root:

```sh
python3 tools/packaging/prepare.py build-deb/dist
# After installing debian/control Build-Depends and devscripts:
bash tools/packaging/debian-build.sh build-deb/dist/*.tar.gz build-deb/native-debian noble binary
bash tools/packaging/debian-build.sh build-deb/dist/*.tar.gz build-deb/ppa noble source
# Arch, as a regular user with official build/runtime dependencies installed:
# Copy build-deb/dist/PKGBUILD to an empty folder, then makepkg -s.
# Fedora: put the archive in ~/rpmbuild/SOURCES and spec in ~/rpmbuild/SPECS,
# then rpmbuild -ba ~/rpmbuild/SPECS/galleoncore.spec.
```

`debian-build.sh` accepts `noble|resolute` and `binary|source` as its last arguments.
`JOBS` defaults to 4. The legacy standalone `build-deb.sh` remains available as
documented in `docs/PACKAGING.md`; the PPA uses debhelper source packaging instead.

For the already-published v0.1.0 orig archive, use Debian revision 2: the
`ppa-repair.yml` manual workflow downloads that exact release archive, verifies
its checksum, and builds `0.1.0-2~noble` and `0.1.0-2~resolute` with the current
Debian packaging rules. The binary build and all ctest suites must pass, and
`check-deb-timestamps.py` rejects pre-2000 payload dates before either source
upload. The original source archive and other distribution artifacts are not
replaced. This corrects Launchpad's rejection of revision 1 binaries with 1970
payload timestamps; Launchpad's resulting binary build state must still be
checked after the upload.
Containers for local validation are provided in `validation.Dockerfile` (built on
the earlier Debian packaging image) and `Fedora.Dockerfile`.

## AppImage and host integration

Build on Ubuntu 24.04, using the dependencies listed in the `appimage` CI job:

```sh
bash tools/packaging/appimage/build.sh build-deb/appimage
```

The official linuxdeploy, Qt plugin and appimagetool downloads are SHA-256 pinned
in `appimage/tools.json`; an upstream continuous asset replacement deliberately
fails verification until its new digest is reviewed. Qt includes X11, Wayland and
offscreen plugins, QML modules and resources. FFmpeg remains an optional host
executable, as do `wpctl`, `gio`, `xdg-open` and systemd. Ubuntu 24.04 or later with
a normal graphical session is the supported baseline; the image does not bundle
the kernel, glibc, udev or a systemd user manager.

Run `./GalleonCore-<version>-x86_64.AppImage` for Bosun, or add `--galleond`
for the daemon. On systems without FUSE, set `APPIMAGE_EXTRACT_AND_RUN=1`.
The AppImage contains its own widgets/wallpapers; configuration stays in the
normal XDG user directories. Its mount path is temporary and must never be used
as the persistent systemd ExecStart path.

Download and extract `GalleonCore-host-integration.tar.gz` beside the AppImage.
These explicit commands install the access rules and user service:

```sh
sudo ./install-hardware-rules.sh
# Keep the AppImage at a stable path; run as your desktop user:
./install-user-service.sh /absolute/path/GalleonCore-0.1.0-x86_64.AppImage
systemctl --user enable --now galleond.service
```

The helper writes a user service invoking the stable AppImage path with
`--galleond`, using extraction mode so FUSE is optional. It never enables it.
If moving the AppImage, re-run the helper. Do not enable both the native package
service and an AppImage override: the user override takes precedence. Stop and
remove `~/.config/systemd/user/galleond.service` to return to the native unit,
then run `systemctl --user daemon-reload`. Hardware rules may remain until
removed explicitly from `/usr/lib/udev/rules.d/` and the modules-load file from
`/usr/lib/modules-load.d/galleoncore.conf`. Never remove rules while using a native
GalleonCore package that owns those files.

The AppImage service uses software built for the module but has the same hardware
permissions as native packages. Container checks do not validate a real desktop
tray, a Wayland/X11 session or the physical keyboard.

Reference workflow structure: [BIT-SCRIPTS/PDF-Equilibrist](https://github.com/BIT-SCRIPTS/PDF-Equilibrist/tree/main/.github/workflows).
Upstream tooling: [Qt deployment plugin](https://github.com/linuxdeploy/linuxdeploy-plugin-qt),
[Launchpad source uploads](https://documentation.ubuntu.com/launchpad/user/how-to/packaging/ppa-package-upload/),
[COPR CLI](https://developer.fedoraproject.org/deployment/copr/copr-cli.html),
[Arch PKGBUILD](https://wiki.archlinux.org/title/PKGBUILD).
