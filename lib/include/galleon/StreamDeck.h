// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "galleon/Protocol.h"
#include "galleon/Transport.h"

#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>

namespace galleon {

// Stream Deck module of the Galleon 100 SD.
//
// Owns a Transport and drives it: keep-alive, input polling, images.
// Byte formats live in galleon::protocol; I/O lives in the Transport.
class StreamDeck : public QObject
{
    Q_OBJECT

public:
    // Without a transport, a HidTransport on the deck's USB IDs is used.
    explicit StreamDeck(std::unique_ptr<Transport> transport = nullptr, QObject *parent = nullptr);
    ~StreamDeck() override;

    bool open();
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString serialNumber() const;

    // Return false while the protocol is not implemented.
    bool setKeyImage(int key, const QImage &image);
    bool setScreenImage(const QImage &image);
    bool setBrightness(int percent);

    // Exposed for tests; the hardware needs protocol::KeepAliveIntervalMs.
    void setKeepAliveInterval(int ms);

Q_SIGNALS:
    void keyPressed(int key);
    void keyReleased(int key);
    void dialRotated(int dial, int steps);
    void dialPressed(int dial);
    void dialReleased(int dial);
    void disconnected();

private:
    void sendKeepAlive();
    void pollInput();
    void dispatch(const protocol::InputEvent &event);

    std::unique_ptr<Transport> m_transport;
    QTimer m_keepAliveTimer;
    QTimer m_inputTimer;
    protocol::InputState m_inputState;
    int m_invalidReports = 0;
};

} // namespace galleon
