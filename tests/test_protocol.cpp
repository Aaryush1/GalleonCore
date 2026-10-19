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
    void keysAreDecodedAsChangesOfState()
    {
        InputState state;
        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + KeyCount, 0);

        report[4 + 3] = 1; // key 3 pressed
        auto result = decodeInputReport(report, state);
        QCOMPARE(int(result.status), int(InputStatus::Ok));
        QCOMPARE(result.events.size(), std::size_t{1});
        QVERIFY((result.events[0] == InputEvent{InputEvent::Type::KeyPressed, 3, 0}));

        // The same report again: no change, no event.
        QVERIFY(decodeInputReport(report, state).events.empty());

        report[4 + 3] = 0; // released
        report[4 + 11] = 1; // and key 11 pressed
        result = decodeInputReport(report, state);
        QCOMPARE(result.events.size(), std::size_t{2});
        QVERIFY((result.events[0] == InputEvent{InputEvent::Type::KeyReleased, 3, 0}));
        QVERIFY((result.events[1] == InputEvent{InputEvent::Type::KeyPressed, 11, 0}));
    }

    void aReleaseNeverComesWithoutAPress()
    {
        InputState state;
        std::vector<std::uint8_t> report = {0x01, 0x00, 0x0c, 0x00};
        report.resize(4 + KeyCount, 0);
        QVERIFY(decodeInputReport(report, state).events.empty()); // nothing was pressed: nothing is released
    }

    void dialButtonsAreDecodedAsChangesOfState()
    {
        InputState state;
        std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x01};
        auto result = decodeInputReport(report, state);
        QCOMPARE(result.events.size(), std::size_t{1});
        QVERIFY((result.events[0] == InputEvent{InputEvent::Type::DialPressed, 1, 0}));

        report[6] = 0;
        result = decodeInputReport(report, state);
        QCOMPARE(result.events.size(), std::size_t{1});
        QVERIFY((result.events[0] == InputEvent{InputEvent::Type::DialReleased, 1, 0}));
    }

    void dialRotationCarriesSignedSteps()
    {
        InputState state;
        // dial 0: +2 clicks, dial 1: -1 click (0xff as a signed byte)
        const std::vector<std::uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x01, 0x02, 0xff};
        const auto result = decodeInputReport(report, state);
        QCOMPARE(int(result.status), int(InputStatus::Ok));
        QCOMPARE(result.events.size(), std::size_t{2});
        QVERIFY((result.events[0] == InputEvent{InputEvent::Type::DialRotated, 0, 2}));
        QVERIFY((result.events[1] == InputEvent{InputEvent::Type::DialRotated, 1, -1}));

        const std::vector<std::uint8_t> still = {0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00};
        QVERIFY(decodeInputReport(still, state).events.empty());
    }

    void malformedReportsAreDroppedAndChangeNothing()
    {
        InputState state;
        std::vector<std::uint8_t> good = {0x01, 0x00, 0x0c, 0x00};
        good.resize(4 + KeyCount, 0);
        good[4] = 1;
        QCOMPARE(decodeInputReport(good, state).events.size(), std::size_t{1});
        const InputState before = state;

        const auto invalidReport = [&](std::vector<std::uint8_t> r) {
            const auto result = decodeInputReport(r, state);
            QCOMPARE(int(result.status), int(InputStatus::Invalid));
            QVERIFY(result.events.empty());
            QVERIFY(state.keys == before.keys && state.dialButtons == before.dialButtons);
        };

        std::vector<std::uint8_t> shortKeys(good.begin(), good.begin() + 10); // truncated
        invalidReport(shortKeys);
        std::vector<std::uint8_t> wrongCount = good;
        wrongCount[2] = 0x0b; // claims 11 keys
        invalidReport(wrongCount);
        std::vector<std::uint8_t> hugeCount = good;
        hugeCount[2] = 0xff;
        hugeCount[3] = 0xff;
        invalidReport(hugeCount);
        std::vector<std::uint8_t> notABoolean = good;
        notABoolean[4 + 5] = 2;
        invalidReport(notABoolean);
        invalidReport({0x01});                                  // only the ID
        invalidReport({0x01, 0x03, 0x03});                      // truncated header
        invalidReport({0x01, 0x03, 0x03, 0x00, 0x01, 0x02});    // dial report one byte short
        invalidReport({0x01, 0x03, 0x04, 0x00, 0x01, 0x02, 0x00}); // wrong declared count
        invalidReport({0x01, 0x03, 0x03, 0x00, 0x00, 0x02, 0x00}); // a button byte of 2
        invalidReport({0x01, 0x03, 0x03, 0x00, 0x01, 0x7f, 0x00}); // +127 steps in one report
        invalidReport({0x01, 0x03, 0x03, 0x00, 0x01, 0x80, 0x00}); // -128
    }

    void otherReportsAreIgnoredNotInvalid()
    {
        InputState state;
        QCOMPARE(int(decodeInputReport({}, state).status), int(InputStatus::Ignored));
        const std::vector<std::uint8_t> otherId = {0x07, 0x00, 0x0c, 0x00, 0x01};
        QCOMPARE(int(decodeInputReport(otherId, state).status), int(InputStatus::Ignored));
        const std::vector<std::uint8_t> otherKind = {0x01, 0x09, 0x00, 0x00};
        QCOMPARE(int(decodeInputReport(otherKind, state).status), int(InputStatus::Ignored));
        const std::vector<std::uint8_t> otherDialKind = {0x01, 0x03, 0x03, 0x00, 0x05, 0x01, 0x01};
        QCOMPARE(int(decodeInputReport(otherDialKind, state).status), int(InputStatus::Ignored));
    }

    void everyPrefixAndEveryByteOfAReportIsSafe()
    {
        // No input may read out of bounds or crash, whatever it holds.
        std::vector<std::uint8_t> keys = {0x01, 0x00, 0x0c, 0x00};
        keys.resize(4 + KeyCount, 1);
        std::vector<std::uint8_t> dials = {0x01, 0x03, 0x03, 0x00, 0x01, 0x02, 0xfe};
        InputState state;
        for (const auto &report : {keys, dials}) {
            for (std::size_t n = 0; n <= report.size(); ++n)
                (void)decodeInputReport(std::span<const std::uint8_t>(report).first(n), state);
            for (std::size_t at = 0; at < report.size(); ++at) {
                for (int value = 0; value < 256; ++value) {
                    std::vector<std::uint8_t> mutated = report;
                    mutated[at] = static_cast<std::uint8_t>(value);
                    (void)decodeInputReport(mutated, state);
                }
            }
        }
    }

    void keepAliveIsReport3Command27Padded()
    {
        const auto report = keepAliveReport();
        QCOMPARE(report.size(), FeatureReportSize);
        QCOMPARE(int(report[0]), 0x03);
        QCOMPARE(int(report[1]), 0x27);
        QVERIFY(std::all_of(report.begin() + 2, report.end(), [](auto b) { return b == 0; }));
    }

    void dialRingReportsCoverTheFourLedsOfTheRightDial()
    {
        // dial 0 -> LEDs 4..7, dial 1 -> LEDs 0..3 (rust-elgato-streamdeck PR 63).
        const auto first = dialRingReports(0, 1, 2, 3);
        QCOMPARE(first.size(), size_t(4));
        for (size_t i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i].size(), FeatureReportSize);
            QCOMPARE(int(first[i][0]), 0x03);
            QCOMPARE(int(first[i][1]), 0x24);
            QCOMPARE(int(first[i][2]), int(4 + i));
            QCOMPARE(int(first[i][3]), 1);
            QCOMPARE(int(first[i][4]), 2);
            QCOMPARE(int(first[i][5]), 3);
        }
        const auto second = dialRingReports(1, 9, 8, 7);
        QCOMPARE(int(second.front()[2]), 0);
        QCOMPARE(int(second.back()[2]), 3);
        QVERIFY(dialRingReports(-1, 0, 0, 0).empty());
        QVERIFY(dialRingReports(2, 0, 0, 0).empty());
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
