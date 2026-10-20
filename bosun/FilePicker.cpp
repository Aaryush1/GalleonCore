// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FilePicker.h"

#include <QFileDialog>
#include <QStandardPaths>

namespace bosun {

QString FilePicker::picture() // NOLINT(readability-convert-member-functions-to-static): QML invokes this QObject method.
{
    return QFileDialog::getOpenFileName(nullptr, tr("Choose a picture"),
                                       QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                       tr("Pictures (*.jpg *.jpeg *.png *.webp *.bmp);;All files (*)"));
}

QString FilePicker::video() // NOLINT(readability-convert-member-functions-to-static): QML invokes this QObject method.
{
    return QFileDialog::getOpenFileName(nullptr, tr("Choose a video"),
                                       QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
                                       tr("Videos (*.mp4 *.mkv *.webm *.avi *.mov *.m4v *.mpeg *.mpg *.ogv);;All files (*)"));
}

} // namespace bosun
