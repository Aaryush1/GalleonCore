// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// WRITES to the hardware (lighting only, nothing is saved to flash).
// Sets one lamp to red for 6 seconds, then gives the lighting back.
//
//   lamparray-test-lamp [deck|keyboard] [lamp-id|sweep]      (default: deck 0)
//
// "sweep" lights the lamps one after the other (0.6 s each, the lamp ID is
// printed first) so that each ID can be matched to a physical key. Ctrl-C
// stops it and gives the lighting back.
//
// Report IDs and sizes come from each interface's own descriptor, read on
// 2026-10-18 (docs/protocol/README.md). Same layout on both parts:
//   control      : AutonomousMode, 8 bits (0 = host controls the lamps)
//   multi-update : count, flags, 8 lamp IDs (16 bits), 8 x (R, G, B, I);
//                  51 bytes with the ID
//   deck (2b18, interface 3): control 229, multi-update 227
//   keyboard (2b0c, interface 4): control 6, multi-update 4
//
// Build: gcc -o lamparray-test-lamp lamparray-test-lamp.c $(pkg-config --cflags --libs libusb-1.0)

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

enum { VendorId = 0x1b1c, MultiUpdateSize = 51, MaxLampId = 1023 };

static libusb_device_handle *handle;
static int Interface;
static volatile sig_atomic_t stopRequested;

static void onSignal(int signal)
{
    (void)signal;
    stopRequested = 1;
}

static int setFeature(unsigned char *report, int size)
{
    const int r = libusb_control_transfer(handle, 0x21, 0x09, (3 << 8) | report[0], Interface, report, size, 1000);
    printf("SET report %u (%d bytes): %s\n", report[0], size, r < 0 ? libusb_error_name(r) : "ok");
    return r;
}

int main(int argc, char **argv)
{
    int productId = 0x2b18, controlId = 229, multiUpdateId = 227;
    Interface = 3;
    if (argc > 1 && strcmp(argv[1], "keyboard") == 0) {
        productId = 0x2b0c;
        controlId = 6;
        multiUpdateId = 4;
        Interface = 4;
    } else if (argc > 1 && strcmp(argv[1], "deck") != 0) {
        puts("usage: lamparray-test-lamp [deck|keyboard] [lamp-id]");
        return 2;
    }
    const int sweep = argc > 2 && strcmp(argv[2], "sweep") == 0;
    const int lampCount = productId == 0x2b0c ? 108 : 2; // measured, docs/protocol/README.md
    int lampId = (argc > 2 && !sweep) ? atoi(argv[2]) : 0;
    if (lampId < 0 || lampId > MaxLampId) {
        puts("bad lamp id");
        return 2;
    }

    libusb_context *context = NULL;
    if (libusb_init(&context) != 0)
        return 1;
    handle = libusb_open_device_with_vid_pid(context, VendorId, productId);
    if (!handle) {
        puts("device not found or not accessible");
        return 1;
    }
    if (libusb_claim_interface(handle, Interface) != 0) {
        puts("cannot claim the interface");
        return 1;
    }

    unsigned char hostControl[2] = {controlId, 0};
    unsigned char autonomous[2] = {controlId, 1};

    unsigned char update[MultiUpdateSize];
    memset(update, 0, sizeof update);
    update[0] = multiUpdateId;
    update[1] = 1;  // one lamp
    update[2] = 1;  // LampUpdateComplete
    update[3] = lampId & 0xFF; // lamp ID, 16 bits little-endian
    update[4] = lampId >> 8;
    update[19] = 255; // red of the first lamp (after 3 header bytes + 16 ID bytes)
    update[22] = 255; // intensity

    int ok = setFeature(hostControl, sizeof hostControl) >= 0;
    if (ok && !sweep) {
        ok = setFeature(update, sizeof update) >= 0;
        if (ok) {
            printf("lamp %d should be RED now (6 s)\n", lampId);
            fflush(stdout);
            sleep(6);
        }
    } else if (ok) {
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_handler = onSignal;
        sigaction(SIGINT, &action, NULL);
        sigaction(SIGTERM, &action, NULL);
        for (int lamp = 0; ok && !stopRequested && lamp < lampCount; lamp++) {
            // Two lamps per report: the previous one back to black, this one red.
            memset(update, 0, sizeof update);
            update[0] = multiUpdateId;
            update[1] = lamp > 0 ? 2 : 1;
            update[2] = 1;
            const int first = lamp > 0 ? lamp - 1 : lamp;
            update[3] = first & 0xFF;
            update[4] = first >> 8;
            int redAt = 19;
            if (lamp > 0) {
                update[5] = lamp & 0xFF;
                update[6] = lamp >> 8;
                redAt = 19 + 4; // the second lamp's colour slot
                update[22] = 0; // previous lamp: black, intensity left at 0
            }
            update[redAt] = 255;
            update[redAt + 3] = 255;
            ok = setFeature(update, sizeof update) >= 0;
            printf("lamp %d\n", lamp);
            fflush(stdout);
            usleep(600000);
        }
    }
    setFeature(autonomous, sizeof autonomous); // always give the lighting back

    libusb_release_interface(handle, Interface);
    libusb_close(handle);
    libusb_exit(context);
    return ok ? 0 : 1;
}
