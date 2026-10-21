// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace bosun {

// Qt uses the platform's native file chooser (including the desktop portal when
// available). Cancellation returns an empty path and changes no candidate.
class FilePicker final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString picture();
    Q_INVOKABLE QString video();
    Q_INVOKABLE QString application();
};

} // namespace bosun
