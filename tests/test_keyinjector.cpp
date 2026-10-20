// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyInjector.h"
#ifdef Q_OS_WIN
#include "PlatformBackends.h"
#endif

#include <QtTest>
#include <vector>
#include <utility>

using namespace galleond;

class TestKeyInjector : public QObject
{
    Q_OBJECT
private slots:
#ifdef Q_OS_WIN
    void nativeFactoryDoesNotSendInputAtConstruction()
    {
        const auto keyboard = createKeyInjector();
        QVERIFY(keyboard != nullptr);
        QVERIFY(!keyboard->key(0, true));
    }
#endif

    void whitelist()
    {
        const auto names = allowedKeyNames().split(QStringLiteral(", "));
        QCOMPARE(names.size(), AllowedKeyCount);
        for (const auto &name : names) {
            const auto code = keyCodeForName(name);
            QVERIFY(code.has_value());
            QVERIFY(isAllowedKeyCode(code.value_or(0)));
        }
        QVERIFY(!keyCodeForName(QStringLiteral("enter")));
        QVERIFY(!keyCodeForName(QStringLiteral("KP7")));
        QVERIFY(!keyCodeForName(QString{}));
        QVERIFY(!isAllowedKeyCode(0));
        QVERIFY(!isAllowedKeyCode(AllowedKeyCount + 1));
    }

    void heldKeysReleasedOnShutdown()
    {
        std::vector<std::pair<KeyCode, bool>> events;
        const int code = static_cast<int>(KeyCode::Kp7);
        {
            TrackedKeyboard keyboard([&events](KeyCode key, bool down) {
                events.emplace_back(key, down);
                return true;
            });
            QVERIFY(!keyboard.key(0, true));
            QVERIFY(keyboard.key(code, true));
            QVERIFY(keyboard.key(code, true));
            QCOMPARE(events.size(), std::size_t{1});
        }
        QCOMPARE(events.size(), std::size_t{2});
        QCOMPARE(events.back().first, KeyCode::Kp7);
        QVERIFY(!events.back().second);
    }

    void failedPressDoesNotLeaveHeldKey()
    {
        int attempts = 0;
        {
            TrackedKeyboard keyboard([&attempts](KeyCode, bool) {
                ++attempts;
                return false;
            });
            QVERIFY(!keyboard.key(static_cast<int>(KeyCode::Kp1), true));
        }
        QCOMPARE(attempts, 1);
    }

    void failedReleaseRetriedOnShutdown()
    {
        int releases = 0;
        {
            TrackedKeyboard keyboard([&releases](KeyCode, bool down) {
                if (down)
                    return true;
                return ++releases > 1;
            });
            QVERIFY(keyboard.key(static_cast<int>(KeyCode::KpEnter), true));
            QVERIFY(!keyboard.key(static_cast<int>(KeyCode::KpEnter), false));
        }
        QCOMPARE(releases, 2);
    }
};

QTEST_GUILESS_MAIN(TestKeyInjector)
#include "test_keyinjector.moc"
