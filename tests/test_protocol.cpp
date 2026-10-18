// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <galleon/Protocol.h>

#include <QTest>

#include <algorithm>
#include <array>
#include <numeric>
#include <vector>

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
    void keepAliveIsReport3Command27Padded()
    {
        const auto report = keepAliveReport();
        QCOMPARE(report.size(), FeatureReportSize);
        QCOMPARE(int(report[0]), 0x03);
        QCOMPARE(int(report[1]), 0x27);
        QVERIFY(std::all_of(report.begin() + 2, report.end(), [](auto b) { return b == 0; }));
    }

    void brightnessIsReport3Command08Percent()
    {
        const auto report = brightnessReport(70);
        QCOMPARE(report.size(), FeatureReportSize);
        QCOMPARE(int(report[0]), 0x03);
        QCOMPARE(int(report[1]), 0x08);
        QCOMPARE(int(report[2]), 70);
        QVERIFY(brightnessReport(0).size() == FeatureReportSize);
        QVERIFY(brightnessReport(100).size() == FeatureReportSize);
        QVERIFY(brightnessReport(-1).empty());
        QVERIFY(brightnessReport(101).empty());
    }

    void screenImageIsCutIntoNumberedReports()
    {
        std::vector<std::uint8_t> jpeg(2500);
        std::iota(jpeg.begin(), jpeg.end(), std::uint8_t{0});
        const auto reports = screenImageReports(jpeg, 0, 0, ScreenWidth, ScreenHeight);

        QCOMPARE(reports.size(), std::size_t{3}); // 1008 + 1008 + 484
        std::vector<std::uint8_t> joined;
        for (std::size_t i = 0; i < reports.size(); ++i) {
            const auto &r = reports[i];
            QCOMPARE(r.size(), OutputReportSize);
            QCOMPARE(int(r[0]), 0x02);
            QCOMPARE(int(r[1]), 0x0c);
            QCOMPARE(int(r[6] | (r[7] << 8)), ScreenWidth);   // width, little-endian
            QCOMPARE(int(r[8] | (r[9] << 8)), ScreenHeight);  // height
            QCOMPARE(int(r[10]), i + 1 == reports.size() ? 1 : 0); // last flag
            QCOMPARE(int(r[11] | (r[12] << 8)), int(i));      // part, from 0
            const int length = r[13] | (r[14] << 8);
            QCOMPARE(length, i + 1 == reports.size() ? 484 : 1008);
            joined.insert(joined.end(), r.begin() + 16, r.begin() + 16 + length);
            // Everything after the payload is zero padding.
            QVERIFY(std::all_of(r.begin() + 16 + length, r.end(), [](auto b) { return b == 0; }));
        }
        QCOMPARE(joined, jpeg);
    }

    void screenImageRectangleIsEncoded()
    {
        const std::vector<std::uint8_t> jpeg(100, 0xab);
        const auto reports = screenImageReports(jpeg, 20, 30, 200, 100);
        QCOMPARE(reports.size(), std::size_t{1});
        QCOMPARE(int(reports[0][2] | (reports[0][3] << 8)), 20);
        QCOMPARE(int(reports[0][4] | (reports[0][5] << 8)), 30);
        QCOMPARE(int(reports[0][6] | (reports[0][7] << 8)), 200);
        QCOMPARE(int(reports[0][8] | (reports[0][9] << 8)), 100);
        QCOMPARE(int(reports[0][10]), 1);
    }

    void screenImageRejectsBadInput()
    {
        const std::vector<std::uint8_t> jpeg(10, 1);
        QVERIFY(screenImageReports({}, 0, 0, 10, 10).empty());
        QVERIFY(screenImageReports(jpeg, -1, 0, 10, 10).empty());
        QVERIFY(screenImageReports(jpeg, 0, -1, 10, 10).empty());
        QVERIFY(screenImageReports(jpeg, 0, 0, 0, 10).empty());
        QVERIFY(screenImageReports(jpeg, 0, 0, 10, 0).empty());
        QVERIFY(screenImageReports(jpeg, 1, 0, ScreenWidth, 10).empty());     // sticks out on the right
        QVERIFY(screenImageReports(jpeg, 0, 1, 10, ScreenHeight).empty());    // sticks out at the bottom
        QVERIFY(screenImageReports(jpeg, 0, 0, 65536, 10).empty());
        const std::vector<std::uint8_t> huge(MaxJpegSize + 1, 1);
        QVERIFY(screenImageReports(huge, 0, 0, 10, 10).empty());
        QVERIFY(!screenImageReports(jpeg, ScreenWidth - 10, ScreenHeight - 10, 10, 10).empty()); // exactly inside
    }

    void keyImageIsCutIntoNumberedReports()
    {
        std::vector<std::uint8_t> jpeg(2100);
        std::iota(jpeg.begin(), jpeg.end(), std::uint8_t{7});
        const auto reports = keyImageReports(5, jpeg);

        QCOMPARE(reports.size(), std::size_t{3}); // 1016 + 1016 + 68
        std::vector<std::uint8_t> joined;
        for (std::size_t i = 0; i < reports.size(); ++i) {
            const auto &r = reports[i];
            QCOMPARE(r.size(), OutputReportSize);
            QCOMPARE(int(r[0]), 0x02);
            QCOMPARE(int(r[1]), 0x07);
            QCOMPARE(int(r[2]), 5);
            QCOMPARE(int(r[3]), i + 1 == reports.size() ? 1 : 0);
            const int length = r[4] | (r[5] << 8);
            QCOMPARE(length, i + 1 == reports.size() ? 68 : 1016);
            QCOMPARE(int(r[6] | (r[7] << 8)), int(i));
            joined.insert(joined.end(), r.begin() + 8, r.begin() + 8 + length);
        }
        QCOMPARE(joined, jpeg);
    }

    void keyImageRejectsBadInput()
    {
        const std::vector<std::uint8_t> jpeg(10, 1);
        QVERIFY(keyImageReports(-1, jpeg).empty());
        QVERIFY(keyImageReports(KeyCount, jpeg).empty());
        QVERIFY(keyImageReports(0, {}).empty());
        QVERIFY(!keyImageReports(0, jpeg).empty());
        QVERIFY(!keyImageReports(KeyCount - 1, jpeg).empty());
    }

    void measuredSizesMatchTheDocumentation()
    {
        QCOMPARE(OutputReportSize, std::size_t{1024});
        QCOMPARE(FeatureReportSize, std::size_t{32});
        QCOMPARE(ScreenWidth, 720);
        QCOMPARE(ScreenHeight, 384);
        QCOMPARE(KeyImageSize, 160);
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
