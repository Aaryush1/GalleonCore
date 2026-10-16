// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/StreamDeck.h"

#include "galleon/HidTransport.h"

#include <QDebug>

#include <array>

namespace galleon {

namespace {
constexpr int InputPollIntervalMs = 10;
constexpr std::size_t InputBufferSize = 512;
} // namespace

StreamDeck::StreamDeck(std::unique_ptr<Transport> transport, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
{
    if (!m_transport) {
        m_transport = std::make_unique<HidTransport>(protocol::CorsairVendorId,
                                                     protocol::StreamDeckProductId);
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
    // TODO: JPEG encode and split into reports, per node-elgato-stream-deck.
    return false;
}

bool StreamDeck::setScreenImage(const QImage &image)
{
    if (!isOpen() || image.isNull())
        return false;
    // TODO: confirm the screen resolution, then implement.
    return false;
}

bool StreamDeck::setBrightness(int percent)
{
    if (!isOpen() || percent < 0 || percent > 100)
        return false;
    // TODO: brightness feature report, per node-elgato-stream-deck.
    return false;
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
    std::array<std::uint8_t, InputBufferSize> buffer{};
    const int read = m_transport->read(buffer);
    if (read < 0) {
        qWarning() << "galleon: Stream Deck module disconnected";
        close();
        Q_EMIT disconnected();
        return;
    }
    if (read == 0)
        return;

    const auto report = std::span<const std::uint8_t>(buffer).first(static_cast<std::size_t>(read));
    for (const auto &event : protocol::parseInputReport(report))
        dispatch(event);
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
