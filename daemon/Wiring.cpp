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
        std::uint32_t bar = 0xffffff;
        if (p->wallpaperMode())
            bar = 0xff00ff;
        else if (p->themeMode() && theme)
            bar = theme->rgb;
        s->setBottomBar(bar, p->wallpaperMode());
    };
    for (auto signal : {&PagesController::themeChanged, &PagesController::themeModeChanged,
                        &PagesController::wallpaperModeChanged})
        QObject::connect(p, signal, s, showState);
    QObject::connect(p, &PagesController::wallpaperStep, s, [s](int steps) { s->stepWallpaper(steps); });
    QObject::connect(p, &PagesController::screenConfigChanged, s, [p, s] { s->setScreenConfig(p->screenConfig()); });
    QObject::connect(p, &PagesController::wallpaperCountdown, s, [s](int left) { s->setCountdown(left); });
    QObject::connect(s, &ScreenController::choosingLabelShown, p, [p] { p->wallpaperLabelShown(); });
    showState();
    s->setScreenConfig(p->screenConfig());
}

} // namespace galleond
