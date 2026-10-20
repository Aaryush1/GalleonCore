// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <TrayMenu.h>

#include <QTest>

using bosun::TrayEntry;
using bosun::trayEntries;

namespace {

QList<TrayEntry::Action> actionsOf(const QList<TrayEntry> &entries)
{
    QList<TrayEntry::Action> actions;
    for (const TrayEntry &entry : entries)
        actions << entry.action;
    return actions;
}

} // namespace

class TestTrayMenu : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theFirstEntryShowsOrHidesTheWindow()
    {
        QCOMPARE(trayEntries(true, true).first().label, QStringLiteral("Hide"));
        QCOMPARE(trayEntries(false, true).first().label, QStringLiteral("Show"));
        QCOMPARE(trayEntries(true, false).first().action, TrayEntry::Action::ToggleWindow);
    }

    void whenGalleondRunsOneCanQuitAndStopItOrQuitAlone()
    {
        const auto entries = trayEntries(true, true);
        QCOMPARE(actionsOf(entries), (QList<TrayEntry::Action>{TrayEntry::Action::ToggleWindow, TrayEntry::Action::QuitAndStop, TrayEntry::Action::Quit}));
        QVERIFY(entries.at(1).label.contains("stop galleond"));
        QVERIFY(entries.at(2).label.contains("keeps running")); // said, so that nobody is surprised
    }

    void whenGalleondDoesNotRunOneCanStartItAndStoppingIsNotOffered()
    {
        const auto entries = trayEntries(false, false);
        QCOMPARE(actionsOf(entries), (QList<TrayEntry::Action>{TrayEntry::Action::ToggleWindow, TrayEntry::Action::StartDaemon, TrayEntry::Action::Quit}));
        QVERIFY(!actionsOf(entries).contains(TrayEntry::Action::QuitAndStop));
    }

    void everyEntryHasAWordToShow()
    {
        for (const bool shown : {false, true}) {
            for (const bool running : {false, true}) {
                for (const TrayEntry &entry : trayEntries(shown, running))
                    QVERIFY(!entry.label.isEmpty());
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestTrayMenu)
#include "test_traymenu.moc"
