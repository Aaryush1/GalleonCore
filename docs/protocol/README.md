# Protocol notes

Facts about the Galleon 100 SD USB protocol, each with its source.

## Sources

- node-elgato-stream-deck (Galleon support since v7.5.0): https://github.com/Julusian/node-elgato-stream-deck
- galleon-deck `docs/PROTOCOL.md`: https://github.com/NLMP-DDHS/galleon-deck/blob/main/docs/PROTOCOL.md

## Stream Deck module (`1b1c:2b18`)

- Host mode only while a keep-alive feature report is sent every 500 ms. (galleon-deck)
- Key and screen images are JPEG. (galleon-deck)
- Screen resolution: **unconfirmed**.

## Keyboard part (`1b1c:2b0c`)

- RGB uses Corsair's "V2"/Bragi protocol on USB interface 1. (galleon-deck)
- Read-only queries are safe. A colour write with the wrong buffer layout froze typing until the keyboard was unplugged. (galleon-deck)

## Captures

Put raw captures under `captures/`, named `YYYY-MM-DD-<what>.pcapng`, with a short note on what was done during the capture.
