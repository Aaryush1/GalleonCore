<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Security policy

## Supported versions

| Version | Security fixes |
| --- | --- |
| Current `main` | Maintained during active development |
| Latest tagged release, once published | Fixes are provided in the next release |
| Older releases or downstream modifications | No separate backport commitment |

GalleonCore is an actively developed community project. There is no guaranteed
response time or enterprise support service.

## Report a vulnerability privately

Email **paulwoisard@gmail.com** with the subject `GalleonCore security report`.
If GitHub private vulnerability reporting is enabled, you can also use
[Report a vulnerability](https://github.com/Paullux/GalleonCore/security/advisories/new).
Do not use a public issue for an unpatched vulnerability, credentials or raw USB
captures. Do not send encrypted mail without first agreeing on a verified public key.

Include the affected version/commit, distribution and Qt version, reproduction
steps or a minimal proof of concept, expected impact and any mitigations you found.
Redact serial numbers, keystrokes, private media and tokens. Use a fake transport
or a minimal input fixture when possible; reproducing an issue must not require
writing to the keyboard's flash or disrupting someone else's device.

The maintainer will review the report, coordinate any additional investigation
privately, discuss a fix and agree on disclosure and credit with the reporter.
Security fixes target the maintained code and are released when ready. Please
allow time to investigate before disclosing exploitation details.

## Relevant boundaries

Reports about malformed HID input, unsafe media/configuration parsing, unbounded
resource use, command execution, D-Bus access or excessive device permissions are
particularly useful. galleond is intended to run as the logged-in user, not root;
udev grants access to the active local session. The project does not write
persistent keyboard settings. Hardware availability issues should still be
reported, with a distinction between temporary device failure and host compromise.

Normal bugs and feature requests belong in
[GitHub issues](https://github.com/Paullux/GalleonCore/issues).
