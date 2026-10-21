// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <galleon/HidTransport.h>
#include <hidapi.h>
#include <QTest>
#include <array>

namespace {
struct FakeHid
{
    hid_device device;
    std::array<char, 5> path{'f', 'a', 'k', 'e', '\0'};
    hid_device_info info{.path = path.data(), .interface_number = 0, .next = nullptr};
    int nonblockingResult = 0;
    int closes = 0;
    int lastTimeout = -1;
    int lastNonblocking = -1;
};
FakeHid &state()
{
    static FakeHid instance;
    return instance;
}
}
int hid_init() { return 0; }
void hid_close(hid_device * /*device*/) { ++state().closes; }
hid_device_info *hid_enumerate(unsigned short /*vendor*/, unsigned short /*product*/) { return &state().info; }
void hid_free_enumeration(hid_device_info * /*devices*/) {}
hid_device *hid_open_path(const char * /*path*/) { return &state().device; }
const wchar_t *hid_error(hid_device * /*device*/) { return L"simulated HID error"; }
int hid_set_nonblocking(hid_device * /*device*/, int enabled)
{
    state().lastNonblocking = enabled;
    return state().nonblockingResult;
}
int hid_get_serial_number_string(hid_device * /*device*/, wchar_t * /*buffer*/, std::size_t /*size*/) { return -1; }
int hid_send_feature_report(hid_device * /*device*/, const unsigned char * /*data*/, std::size_t size) { return static_cast<int>(size); }
int hid_write(hid_device * /*device*/, const unsigned char * /*data*/, std::size_t size) { return static_cast<int>(size); }
int hid_read_timeout(hid_device * /*device*/, unsigned char * /*data*/, std::size_t /*size*/, int timeout)
{
    state().lastTimeout = timeout;
    return 0;
}

class TestHidTransport : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        state().nonblockingResult = 0;
        state().closes = 0;
        state().lastTimeout = -1;
        state().lastNonblocking = -1;
    }
    void failedNonblockingSetupClosesHandleAndAllowsRetry()
    {
        galleon::HidTransport transport(0x1b1c, 0x2b18, 0);
        state().nonblockingResult = -1;
        QVERIFY(!transport.open());
        QVERIFY(!transport.isOpen());
        QCOMPARE(state().closes, 1);
        state().nonblockingResult = 0;
        QVERIFY(transport.open());
        QCOMPARE(state().lastNonblocking, 1);
    }
    void everyReadHasAnExplicitZeroTimeout()
    {
        galleon::HidTransport transport(0x1b1c, 0x2b18, 0);
        QVERIFY(transport.open());
        std::array<std::uint8_t, 512> buffer{};
        QCOMPARE(transport.read(buffer), 0);
        QCOMPARE(state().lastTimeout, 0);
        transport.close();
        QCOMPARE(transport.read(buffer), -1);
        QCOMPARE(state().closes, 1);
    }
};
QTEST_GUILESS_MAIN(TestHidTransport)
#include "test_hidtransport.moc"
