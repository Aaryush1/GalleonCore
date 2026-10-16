// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <galleon/Protocol.h>

#include <QTest>

#include <array>

using namespace galleon::protocol;

class TestProtocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyReportYieldsNoEvent()
    {
        QVERIFY(parseInputReport({}).empty());
    }

    void garbageReportsNeverCrash()
    {
        // Robustness only: the expected events will come from real captures.
        const std::array<std::uint8_t, 1> tiny{0xff};
        QVERIFY(parseInputReport(tiny).empty());

        std::array<std::uint8_t, 512> large{};
        large.fill(0xff);
        parseInputReport(large);
    }

    void usbIdsMatchDocumentation()
    {
        QCOMPARE(CorsairVendorId, std::uint16_t{0x1b1c});
        QCOMPARE(StreamDeckProductId, std::uint16_t{0x2b18});
        QCOMPARE(KeyboardProductId, std::uint16_t{0x2b0c});
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
