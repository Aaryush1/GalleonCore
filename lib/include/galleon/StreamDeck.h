// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "galleon/Protocol.h"
#include "galleon/Transport.h"

#include <QImage>
#include <QElapsedTimer>
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
    // `flush`: the module shows a picture only when a second transfer follows it (measured on the
    // module, see docs/protocol/README.md): by default every picture is sent twice in a row, so it is
    // shown at once. Pictures that stream (a video) pass false: the next one flushes the last.
    bool setKeyImage(int key, const QImage &image, bool flush = true);
    // `quality`: JPEG quality, 1..100. A lower one makes a smaller picture, quicker to send:
    // for pictures that change at each step of a dial. 90 otherwise.
    bool setScreenImage(const QImage &image, int quality = 90, bool flush = true);
    bool setBrightness(int percent);
    // Lights the ring around a dial (0 or 1) with one colour; see protocol::dialRingReports.
    bool setDialRing(int dial, std::uint8_t red, std::uint8_t green, std::uint8_t blue);

    // Sends the keep-alive report now, outside its timer (used to test whether it makes the module
    // show a picture it already received). The timer keeps its own rhythm.
    bool sendKeepAliveNow();

    // Logs the first bytes and the status of every input report. For checking
    // what the module really sends; off by default (it is chatty).
    void setTraceInput(bool enabled);

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
    bool writeReports(const std::vector<std::vector<std::uint8_t>> &reports);
    void pollInput();
    bool pollOneReport(); // false when there is nothing more to read (or the module is gone)
    void dispatch(const protocol::InputEvent &event);

    std::unique_ptr<Transport> m_transport;
    QTimer m_keepAliveTimer;
    QElapsedTimer m_sinceKeepAlive;
    QTimer m_inputTimer;
    protocol::InputState m_inputState;
    int m_invalidReports = 0;
    bool m_traceInput = false;
};

} // namespace galleon
