// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/StreamDeck.h"

#include <hidapi/hidapi.h>

#include <QDebug>

namespace galleon {

namespace {
constexpr int KeepAliveIntervalMs = 500;
constexpr int InputPollIntervalMs = 10;
} // namespace

StreamDeck::StreamDeck(QObject *parent)
    : QObject(parent)
{
    hid_init();

    m_keepAliveTimer.setInterval(KeepAliveIntervalMs);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &StreamDeck::sendKeepAlive);

    m_inputTimer.setInterval(InputPollIntervalMs);
    connect(&m_inputTimer, &QTimer::timeout, this, &StreamDeck::pollInput);
}

StreamDeck::~StreamDeck()
{
    close();
    hid_exit();
}

bool StreamDeck::open()
{
    if (m_device)
        return true;

    m_device = hid_open(CorsairVendorId, StreamDeckProductId, nullptr);
    if (!m_device) {
        qWarning() << "galleon: cannot open Stream Deck module"
                   << QString::fromWCharArray(hid_error(nullptr));
        return false;
    }

    hid_set_nonblocking(m_device, 1);
    sendKeepAlive();
    m_keepAliveTimer.start();
    m_inputTimer.start();
    return true;
}

void StreamDeck::close()
{
    m_keepAliveTimer.stop();
    m_inputTimer.stop();
    if (m_device) {
        hid_close(m_device);
        m_device = nullptr;
    }
}

bool StreamDeck::isOpen() const
{
    return m_device != nullptr;
}

QString StreamDeck::serialNumber() const
{
    if (!m_device)
        return {};
    wchar_t buffer[128] = {};
    if (hid_get_serial_number_string(m_device, buffer, 128) != 0)
        return {};
    return QString::fromWCharArray(buffer);
}

bool StreamDeck::setKeyImage(int key, const QImage &image)
{
    Q_UNUSED(image)
    if (!m_device || key < 0 || key >= KeyCount)
        return false;
    // TODO: JPEG encode and send in chunks, per node-elgato-stream-deck.
    return false;
}

bool StreamDeck::setScreenImage(const QImage &image)
{
    Q_UNUSED(image)
    if (!m_device)
        return false;
    // TODO: confirm the screen resolution, then implement.
    return false;
}

bool StreamDeck::setBrightness(int percent)
{
    Q_UNUSED(percent)
    // TODO: brightness feature report, per node-elgato-stream-deck.
    return false;
}

void StreamDeck::sendKeepAlive()
{
    if (!m_device)
        return;
    // TODO: send the keep-alive feature report described in galleon-deck's
    // docs/PROTOCOL.md. Do not guess its bytes.
}

void StreamDeck::pollInput()
{
    if (!m_device)
        return;

    unsigned char report[512];
    const int read = hid_read(m_device, report, sizeof(report));
    if (read < 0) {
        qWarning() << "galleon: Stream Deck module disconnected";
        close();
        Q_EMIT disconnected();
        return;
    }
    if (read == 0)
        return;

    // TODO: decode key and dial reports, then emit the matching signals.
}

} // namespace galleon
