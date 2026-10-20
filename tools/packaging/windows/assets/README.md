<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Setup artwork

The icon is derived from `bosun/icons/galleon-bosun.svg`, drawn for this project.
The editable header and welcome SVGs reuse its wheel, 12 colored keys, charcoal
background and amber accent. No generated artwork or third-party illustrations
are used. All these installer graphics retain GPL-3.0-or-later.

Render from the repository root with ImageMagick (and its SVG renderer):

```sh
magick -background none bosun/icons/galleon-bosun.svg -define icon:auto-resize=256,128,64,48,32,16 tools/packaging/windows/assets/galleon-bosun.ico
magick tools/packaging/windows/assets/welcome.svg -alpha off -type TrueColor BMP3:tools/packaging/windows/assets/welcome.bmp
magick tools/packaging/windows/assets/header.svg -alpha off -type TrueColor BMP3:tools/packaging/windows/assets/header.bmp
```

The BMPs use Modern UI's 164×314 welcome and 150×57 header dimensions.
The multi-resolution ICO is shared by Setup, the uninstaller and both native
executables so Explorer and the shortcuts show Bosun's real icon.
