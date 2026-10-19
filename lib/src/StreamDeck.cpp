// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/StreamDeck.h"

#include "galleon/HidTransport.h"

#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QImageWriter>

#include <algorithm>
#include <array>

namespace galleon {

namespace {
constexpr int InputPollIntervalMs = 10;
constexpr int MaxReportsPerPoll = 64; // the most that one tick reads
constexpr std::size_t InputBufferSize = 512;
constexpr int JpegQuality = 90;

// Scales to the exact size the module draws (the module does no scaling) and
// encodes a baseline JPEG, which is what the module is documented to take.
QByteArray encodeJpeg(const QImage &image, int width, int height)
{
    QImage prepared = image.convertToFormat(QImage::Format_RGB888);
    if (prepared.width() != width || prepared.height() != height)
        prepared = prepared.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(JpegQuality);
    if (!writer.write(prepared))
        return {};
    return bytes;
}

bool writeAll(Transport &transport, const std::vector<std::vector<std::uint8_t>> &reports)
{
    if (reports.empty())
        return false;
    for (const auto &report : reports) {
        if (!transport.write(report))
            return false;
    }
    return true;
}
} // namespace

StreamDeck::StreamDeck(std::unique_ptr<Transport> transport, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
{
    if (!m_transport) {
        m_transport = std::make_unique<HidTransport>(protocol::CorsairVendorId,
                                                     protocol::StreamDeckProductId,
                                                     protocol::ControlInterface);
    }

    m_keepAliveTimer.setInterval(protocol::KeepAliveIntervalMs);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &StreamDeck::sendKeepAlive);

    m_inputTimer.setInterval(InputPollIntervalMs);
    connect(&m_inputTimer, &QTimer::timeout, this, &StreamDeck::pollInput);
}

StreamDeck::~StreamDeck()
{
    close();
}

bool StreamDeck::open()
{
    if (!m_transport->open())
        return false;
    sendKeepAlive();
    m_keepAliveTimer.start();
    m_inputTimer.start();
    return true;
}

void StreamDeck::close()
{
    m_inputState = {}; // a device that comes back starts with nothing pressed
    m_keepAliveTimer.stop();
    m_inputTimer.stop();
    m_transport->close();
}

bool StreamDeck::isOpen() const
{
    return m_transport->isOpen();
}

QString StreamDeck::serialNumber() const
{
    return m_transport->serialNumber();
}

bool StreamDeck::setKeyImage(int key, const QImage &image)
{
    if (!isOpen() || key < 0 || key >= protocol::KeyCount || image.isNull())
        return false;
    const QByteArray jpeg = encodeJpeg(image, protocol::KeyImageSize, protocol::KeyImageSize);
    if (jpeg.isEmpty())
        return false;
    const auto bytes = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(jpeg.constData()),
                                                     static_cast<std::size_t>(jpeg.size()));
    return writeAll(*m_transport, protocol::keyImageReports(key, bytes));
}

bool StreamDeck::setScreenImage(const QImage &image)
{
    if (!isOpen() || image.isNull())
        return false;
    const QByteArray jpeg = encodeJpeg(image, protocol::ScreenWidth, protocol::ScreenHeight);
    if (jpeg.isEmpty())
        return false;
    const auto bytes = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(jpeg.constData()),
                                                     static_cast<std::size_t>(jpeg.size()));
    return writeAll(*m_transport,
                    protocol::screenImageReports(bytes, 0, 0, protocol::ScreenWidth, protocol::ScreenHeight));
}

bool StreamDeck::setBrightness(int percent)
{
    if (!isOpen() || percent < 0 || percent > 100)
        return false;
    return m_transport->sendFeatureReport(protocol::brightnessReport(percent));
}

bool StreamDeck::setDialRing(int dial, std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    if (!isOpen())
        return false;
    const auto reports = protocol::dialRingReports(dial, red, green, blue);
    if (reports.empty())
        return false;
    bool ok = true;
    for (const auto &report : reports)
        ok = m_transport->sendFeatureReport(report) && ok;
    return ok;
}

void StreamDeck::setTraceInput(bool enabled)
{
    m_traceInput = enabled;
}

void StreamDeck::setKeepAliveInterval(int ms)
{
    m_keepAliveTimer.setInterval(ms);
}

void StreamDeck::sendKeepAlive()
{
    m_transport->sendFeatureReport(protocol::keepAliveReport());
}

void StreamDeck::pollInput()
{
    // Every report that is waiting, not just one: a module that sends them faster than
    // this timer ticks would otherwise build a queue, and a turn of a dial would be
    // read seconds late. Bounded, so that a flood cannot keep this thread here for good.
    for (int handled = 0; handled < MaxReportsPerPoll; ++handled) {
        if (!pollOneReport())
            return;
    }
}

bool StreamDeck::pollOneReport()
{
    std::array<std::uint8_t, InputBufferSize> buffer{};
    const int read = m_transport->read(buffer);
    if (read < 0) {
        qWarning() << "galleon: Stream Deck module disconnected";
        close();
        Q_EMIT disconnected();
        return false;
    }
    if (read == 0)
        return false;

    const auto report = std::span<const std::uint8_t>(buffer).first(static_cast<std::size_t>(read));
    const protocol::InputResult result = protocol::decodeInputReport(report, m_inputState);
    if (m_traceInput) {
        static constexpr std::array<const char *, 3> names{"ok", "ignored", "invalid"};
        qInfo().noquote() << "input report" << names.at(static_cast<std::size_t>(result.status)) << "["
                          << QByteArray(reinterpret_cast<const char *>(report.data()),
                                        static_cast<qsizetype>(std::min<std::size_t>(report.size(), 20)))
                                 .toHex(' ')
                          << "]";
    }
    if (result.status == protocol::InputStatus::Invalid) {
        // Dropped, and said: the first ones, then one in a hundred, so that a
        // misbehaving device cannot flood the log.
        if (m_invalidReports++ % 100 == 0)
            qWarning() << "galleon: malformed input report dropped, first bytes" << QByteArray(reinterpret_cast<const char *>(report.data()), static_cast<qsizetype>(std::min<std::size_t>(report.size(), 8))).toHex(' ');
        return true;
    }
    for (const auto &event : result.events)
        dispatch(event);
    return isOpen(); // a handler may have closed the module
}

void StreamDeck::dispatch(const protocol::InputEvent &event)
{
    using Type = protocol::InputEvent::Type;
    switch (event.type) {
    case Type::KeyPressed:
        Q_EMIT keyPressed(event.index);
        break;
    case Type::KeyReleased:
        Q_EMIT keyReleased(event.index);
        break;
    case Type::DialRotated:
        Q_EMIT dialRotated(event.index, event.value);
        break;
    case Type::DialPressed:
        Q_EMIT dialPressed(event.index);
        break;
    case Type::DialReleased:
        Q_EMIT dialReleased(event.index);
        break;
    }
}

} // namespace galleon
