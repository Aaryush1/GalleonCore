// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Wiring.h"

#include "PagesController.h"
#include "ScreenController.h"

namespace galleond {

void connectPagesToScreen(PagesController &pages, ScreenController &screen)
{
    PagesController *p = &pages;
    ScreenController *s = &screen;

    const auto showState = [p, s] {
        const Theme *theme = p->currentTheme();
        s->setTheme(theme ? theme->name : QString(), theme ? theme->rgb : 0, p->themeMode());
        const std::uint32_t bar = p->wallpaperMode() ? 0xff00ff : p->themeMode() && theme ? theme->rgb : 0xffffff;
        s->setBottomBar(bar, p->wallpaperMode());
    };
    for (auto signal : {&PagesController::themeChanged, &PagesController::themeModeChanged,
                        &PagesController::wallpaperModeChanged})
        QObject::connect(p, signal, s, showState);
    QObject::connect(p, &PagesController::wallpaperStep, s, [s](int steps) { s->stepWallpaper(steps); });
    QObject::connect(p, &PagesController::screenConfigChanged, s, [p, s] { s->setScreenConfig(p->screenConfig()); });
    showState();
    s->setScreenConfig(p->screenConfig());
}

} // namespace galleond
