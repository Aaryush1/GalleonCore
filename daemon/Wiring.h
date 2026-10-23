// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace galleond {

class PagesController;
class ScreenController;
class ScreensaverController;

// Makes the screen follow the pages: the theme's name and colour along the top,
// the line along the bottom (magenta while the wallpaper is being chosen, the
// theme's colour while the theme is, white for the player control), the
// wallpaper steps of the dial, and the screen settings. Used by galleond and by
// the tests, so they run the same code. Both objects must outlive the connections.
void connectPagesToScreen(PagesController &pages, ScreenController &screen);
void connectGameToScreensaver(ScreenController &screen, ScreensaverController &screensaver);

} // namespace galleond
