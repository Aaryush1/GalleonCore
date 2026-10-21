// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

// Minimal fake ABI for compiling the real HidTransport without a HID backend.
struct hid_device {};
struct hid_device_info {
    char *path;
    int interface_number;
    hid_device_info *next;
};
int hid_init();
void hid_close(hid_device *device);
hid_device_info *hid_enumerate(unsigned short vendor, unsigned short product);
void hid_free_enumeration(hid_device_info *devices);
hid_device *hid_open_path(const char *path);
const wchar_t *hid_error(hid_device *device);
int hid_set_nonblocking(hid_device *device, int enabled);
int hid_get_serial_number_string(hid_device *device, wchar_t *buffer, std::size_t size);
int hid_send_feature_report(hid_device *device, const unsigned char *data, std::size_t size);
int hid_write(hid_device *device, const unsigned char *data, std::size_t size);
int hid_read_timeout(hid_device *device, unsigned char *data, std::size_t size, int timeout);
