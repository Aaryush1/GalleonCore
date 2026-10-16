// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>

struct hid_device_;

namespace galleon {

// USB IDs of the Galleon 100 SD (see docs/ROADMAP.md).
constexpr unsigned short CorsairVendorId = 0x1b1c;
constexpr unsigned short StreamDeckProductId = 0x2b18;

constexpr int KeyCount = 12;
constexpr int DialCount = 2;

// Stream Deck module of the Galleon 100 SD.
//
// The module only stays in host mode while a keep-alive feature report is
// sent every 500 ms; otherwise the firmware falls back to its numpad mode.
class StreamDeck : public QObject
{
    Q_OBJECT

public:
    explicit StreamDeck(QObject *parent = nullptr);
    ~StreamDeck() override;

    bool open();
    void close();
    bool isOpen() const;

    QString serialNumber() const;

    // Return false while the protocol is not implemented.
    bool setKeyImage(int key, const QImage &image);
    bool setScreenImage(const QImage &image);
    bool setBrightness(int percent);

Q_SIGNALS:
    void keyPressed(int key);
    void keyReleased(int key);
    void dialRotated(int dial, int steps);
    void dialPressed(int dial);
    void disconnected();

private:
    void sendKeepAlive();
    void pollInput();

    hid_device_ *m_device = nullptr;
    QTimer m_keepAliveTimer;
    QTimer m_inputTimer;
};

} // namespace galleon
