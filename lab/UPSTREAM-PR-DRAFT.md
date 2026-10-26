Title: docs(protocol): live per-key lighting confirmed on firmware 2.3.88

Follows up on #56.

## Problem

`docs/protocol/README.md` says no live per-key lighting stream had been seen, and that what file 0x0022 controls is unknown.

## Change

Docs only, in `docs/protocol/`:

- A new section, "Live per-key lighting (Windows, 2026-10-25)", with:
  - the host-controlled sequence on file 0x0022;
  - the LED index mapping (USB HID usage, Copilot at 0x81, the top-edge strip from 0x82, with that map unfinished);
  - release behaviour after a crash, on suspend, after a replug and when the Web Hub connects;
  - rate measurements.
- The three places that called the stream or file 0x22 unknown now point to that section. One of them is in `webhub-lighting-analysis.md`.

No code changes.

## Validation

These are owner-confirmed hardware results. Every visual result was watched on the keyboard, and the counts come from the test tool's command log. The tool and run notes are in the fork's `lab/` folder: https://github.com/Aaryush1/GalleonCore/tree/key-lighting/lab. I didn't run `tools/check.py` locally (no Docker on this Windows machine). The change touches only Markdown.

Thank you for GalleonCore. It gave us the starting point for all of this.
