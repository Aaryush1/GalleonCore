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
// Every step sends a FULL FRAME (all lamps, black except the target), in
// batches of 8 lamps with the "update complete" flag on the last batch only,
// like the OpenRGB plugin does. Reports are spaced by the device's minimum
// update interval (3 ms keyboard, 20 ms deck, from the attributes report).
//
// Report IDs and sizes come from each interface's own descriptor, read on
// 2026-10-18 (docs/protocol/README.md). Same layout on both parts:
//   control      : AutonomousMode, 8 bits (0 = host controls the lamps)
//   multi-update : count, flags, 8 lamp IDs (16 bits), 8 x (R, G, B, I);
//                  51 bytes with the ID
//   deck (2b18, interface 3): control 229, multi-update 227, 2 lamps, 20 ms
//   keyboard (2b0c, interface 4): control 6, multi-update 4, 108 lamps, 3 ms
//
// Build: gcc -o lamparray-test-lamp lamparray-test-lamp.c $(pkg-config --cflags --libs libusb-1.0)

#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { VendorId = 0x1b1c, MultiUpdateSize = 51, MaxLamps = 108, BatchSize = 8 };

static libusb_device_handle *handle;
static int Interface;
static useconds_t intervalUs;
static volatile sig_atomic_t stopRequested;

static void onSignal(int signal)
{
    (void)signal;
    stopRequested = 1;
}

static int setFeature(unsigned char *report, int size, int verbose)
{
    usleep(intervalUs);
    const int r = libusb_control_transfer(handle, 0x21, 0x09, (3 << 8) | report[0], Interface, report, size, 1000);
    if (verbose || r < 0)
        printf("SET report %u (%d bytes): %s\n", report[0], size, r < 0 ? libusb_error_name(r) : "ok");
    return r;
}

// Sends the whole frame: lamp i gets red[i] only (green and blue stay 0).
static int sendFrame(int multiUpdateId, int lampCount, const unsigned char *red)
{
    for (int first = 0; first < lampCount; first += BatchSize) {
        const int count = lampCount - first < BatchSize ? lampCount - first : BatchSize;
        unsigned char report[MultiUpdateSize];
        memset(report, 0, sizeof report);
        report[0] = (unsigned char)multiUpdateId;
        report[1] = (unsigned char)count;
        report[2] = first + count >= lampCount; // update complete, last batch only
        for (int i = 0; i < count; i++) {
            const int lamp = first + i;
            report[3 + 2 * i] = lamp & 0xFF;
            report[3 + 2 * i + 1] = lamp >> 8;
            report[19 + 4 * i] = red[lamp];
            report[19 + 4 * i + 3] = 255; // intensity
        }
        if (setFeature(report, sizeof report, 0) < 0)
            return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int productId = 0x2b18, controlId = 229, multiUpdateId = 227, lampCount = 2;
    Interface = 3;
    intervalUs = 20000;
    if (argc > 1 && strcmp(argv[1], "keyboard") == 0) {
        productId = 0x2b0c;
        controlId = 6;
        multiUpdateId = 4;
        lampCount = 108;
        Interface = 4;
        intervalUs = 3000;
    } else if (argc > 1 && strcmp(argv[1], "deck") != 0) {
        puts("usage: lamparray-test-lamp [deck|keyboard] [lamp-id|sweep]");
        return 2;
    }
    const int sweep = argc > 2 && strcmp(argv[2], "sweep") == 0;
    const int lampId = (argc > 2 && !sweep) ? atoi(argv[2]) : 0;
    if (lampId < 0 || lampId >= lampCount || lampCount > MaxLamps) {
        printf("lamp id must be 0..%d\n", lampCount - 1);
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

    unsigned char hostControl[2] = {(unsigned char)controlId, 0};
    unsigned char autonomous[2] = {(unsigned char)controlId, 1};
    unsigned char red[MaxLamps];

    int ok = setFeature(hostControl, sizeof hostControl, 1) >= 0;
    if (ok && !sweep) {
        memset(red, 0, sizeof red);
        red[lampId] = 255;
        ok = sendFrame(multiUpdateId, lampCount, red) == 0;
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
            memset(red, 0, sizeof red);
            red[lamp] = 255;
            ok = sendFrame(multiUpdateId, lampCount, red) == 0;
            printf("lamp %d\n", lamp);
            fflush(stdout);
            usleep(600000);
        }
    }
    setFeature(autonomous, sizeof autonomous, 1); // always give the lighting back

    libusb_release_interface(handle, Interface);
    libusb_close(handle);
    libusb_exit(context);
    return ok ? 0 : 1;
}
