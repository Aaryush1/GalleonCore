<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to GalleonCore

Bug reports, documentation, packaging work and code contributions are welcome.
Please follow the [code of conduct](CODE_OF_CONDUCT.md). Report vulnerabilities
privately using [SECURITY.md](SECURITY.md).

## Issues and proposals

For a bug, describe the expected and actual behavior, reproduction steps, commit
or package version, distribution, Qt version and desktop session (Wayland/X11).
Include relevant, redacted logs and state whether the keyboard was connected.
Discuss substantial features or protocol changes in an issue before implementing
them so the scope and hardware evidence can be reviewed.

USB captures can contain serial numbers and keystrokes. Share only sanitized
extracts, never raw personal captures or configuration secrets.

## Development environment

The supported baseline is Ubuntu 24.04 with Qt >= 6.4, CMake >= 3.21 and C++20.
See [README.md](README.md#building) for dependencies and
[CLAUDE.md](CLAUDE.md) for project conventions. Typical local checks are:

```sh
cmake -S . -B build -DGALLEON_WERROR=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
reuse lint
```

Run tests through **ctest**: it isolates each D-Bus session and test configuration.
Do not run test executables against your normal desktop session.

The CI-equivalent check uses Docker and the supported compiler/Qt versions:

```sh
python3 tools/check.py quick   # incremental checks during a small change
python3 tools/check.py full    # complete validation before opening or merging a PR
python3 tools/check.py status  # confirm the validation matches this tree
```

A targeted result is not a full validation. The full run includes GCC, Clang with
ASan/UBSan, fuzzing, clang-tidy, qmllint, tool tests and REUSE. Packaging instructions
and publication setup are in [tools/packaging/README.md](tools/packaging/README.md).

## Implementation and hardware rules

- Keep protocol parsing and geometry pure, transport limited to I/O, and daemon
  behavior testable with a fake transport. Add a regression test for behavior changes.
- Validate external bytes, JSON, images and videos before using them. Keep caches
  and other long-lived state bounded; prefer Qt ownership or smart pointers.
- Cite captures or an identified upstream implementation for protocol facts.
  Never invent protocol bytes or write persistent settings to the keyboard's flash.
- Only galleond opens the device; Bosun communicates through D-Bus and validated
  configuration. Shared QML widgets must not require Plasma or Kirigami.
- Do not send hardware test commands to another person's keyboard. Document the
  proposed test and distinguish automated checks from owner-confirmed hardware results.

## Pull requests

Create a focused branch, make a clear commit and open a PR against `main` with the
problem, resulting behavior and validation performed. Keep `README.md` and
`README.fr.md` synchronized when changing public documentation. Write source,
comments and other public documents in English. Update the roadmap when a feature
is completed, while keeping unconfirmed hardware behavior explicitly unconfirmed.

Main is protected: changes go through a PR with passing checks. Do not bypass a
failed build or publish packages whose validation failed. Publication credentials
belong in GitHub secrets, never in the source tree.

## Licenses and assets

Use the existing SPDX headers: `SPDX-FileCopyrightText: 2026 Paul Woisard` for
existing project work (add accurate attribution for new contributors) and the
appropriate identifier: LGPL-3.0-or-later in `lib/`, GPL-3.0-or-later for application
code and documentation. See [LICENSE.md](LICENSE.md) for the complete mapping.
Images and videos follow [ASSETS.md](ASSETS.md): record provenance and licensing,
and do not add unlicensed characters, logos or personal footage. `reuse lint`
must pass. Contributions retain their applicable license and attribution.
