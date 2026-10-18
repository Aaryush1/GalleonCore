// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// WRITES to the hardware (lighting only, nothing is saved to flash).
// Sets lamp 0 of the Stream Deck part (the LED beside the left dial, by
// position) to red for 6 seconds, then gives the lighting back to the keyboard.
//
// Report IDs and sizes come from the interface's own descriptor, read with
// lamparray-probe on 2026-10-18 (docs/protocol/README.md):
//   229  LampArrayControlReport  : AutonomousMode, 8 bits (0 = host controls)
//   227  LampMultiUpdateReport   : count, flags, 8 lamp IDs (16 bits),
//                                  8 x (R, G, B, I); 51 bytes with the ID
//
// Build: gcc -o lamparray-test-lamp lamparray-test-lamp.c $(pkg-config --cflags --libs libusb-1.0)

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { VendorId = 0x1b1c, ProductId = 0x2b18, Interface = 3 };
enum { ControlId = 229, MultiUpdateId = 227, MultiUpdateSize = 51 };

static libusb_device_handle *handle;

static int setFeature(unsigned char *report, int size)
{
    const int r = libusb_control_transfer(handle, 0x21, 0x09, (3 << 8) | report[0], Interface, report, size, 1000);
    printf("SET report %u (%d bytes): %s\n", report[0], size, r < 0 ? libusb_error_name(r) : "ok");
    return r;
}

int main(void)
{
    libusb_context *context = NULL;
    if (libusb_init(&context) != 0)
        return 1;
    handle = libusb_open_device_with_vid_pid(context, VendorId, ProductId);
    if (!handle) {
        puts("device not found or not accessible");
        return 1;
    }
    if (libusb_claim_interface(handle, Interface) != 0) {
        puts("cannot claim the interface");
        return 1;
    }

    unsigned char hostControl[2] = {ControlId, 0};
    unsigned char autonomous[2] = {ControlId, 1};

    unsigned char update[MultiUpdateSize];
    memset(update, 0, sizeof update);
    update[0] = MultiUpdateId;
    update[1] = 1;  // one lamp
    update[2] = 1;  // LampUpdateComplete
    update[3] = 0;  // lamp ID 0, 16 bits little-endian
    update[4] = 0;
    update[19] = 255; // red of the first lamp (after 3 header bytes + 16 ID bytes)
    update[22] = 255; // intensity

    int ok = setFeature(hostControl, sizeof hostControl) >= 0 && setFeature(update, sizeof update) >= 0;
    if (ok) {
        puts("lamp 0 should be RED now (6 s)");
        fflush(stdout);
        sleep(6);
    }
    setFeature(autonomous, sizeof autonomous); // always give the lighting back

    libusb_release_interface(handle, Interface);
    libusb_close(handle);
    libusb_exit(context);
    return ok ? 0 : 1;
}
