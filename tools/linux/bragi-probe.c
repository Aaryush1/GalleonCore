// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY probe of the keyboard's vendor interface (usage page 0xFF42,
// usage 1), Corsair "V2"/Bragi protocol. It sends property GET requests only,
// from a fixed list: nothing that selects a mode, writes a colour or touches
// the lighting is ever sent.
//
// Format, observed in a usbmon capture of the Corsair Web Hub talking to this
// keyboard (firmware v2.3.88, docs/protocol/README.md, section "Vendor
// interface seen from the Corsair Web Hub"): 128-byte unnumbered reports.
//   request  00 01 00 02 <property>              (command 0x02 = read a property)
//   answer   00 00 00 02 <status> <value u32 little-endian>
// The Web Hub itself sent 65 such reads in 71 s. Status 0 = ok; 5 was the
// answer for properties it does not have.
// The `08 02 <property>` format of galleon-deck and OpenRGB gets NO answer on
// this firmware (first version of this probe).
// Properties asked: only ones the Web Hub read with status 0:
//   0x01 (4), 0x02 (1000, probably brightness), 0x05 (0x0501), 0x0a (5),
//   0x13 (firmware, bytes 02 03 58 = 2.3.88), 0x41 (2), 0x6e (1).
//
// Build: gcc -Wall -o bragi-probe bragi-probe.c
// Run:   ./bragi-probe          (close OpenRGB and OpenLinkHub first)

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum { PacketSize = 128, MaxHidraw = 64, TimeoutMs = 500 };

static const uint8_t CommandGet = 0x02;
static const uint8_t Properties[] = {0x01, 0x02, 0x05, 0x0a, 0x13, 0x41, 0x6e};
// Report descriptor start of the interface: Usage Page (0xFF42), Usage (1).
static const uint8_t VendorDescriptorStart[] = {0x06, 0x42, 0xff, 0x09, 0x01};

// Is /dev/hidrawN a USB HID device 1b1c:2b0c? Checked in sysfs, before opening.
static int isGalleonKeyboard(int index)
{
    char path[96];
    snprintf(path, sizeof path, "/sys/class/hidraw/hidraw%d/device/uevent", index);
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    char line[160];
    int match = 0;
    while (fgets(line, sizeof line, file)) {
        if (strncmp(line, "HID_ID=0003:00001B1C:00002B0C", 29) == 0)
            match = 1;
    }
    fclose(file);
    return match;
}

static int hasVendorDescriptor(int fd)
{
    int size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size < (int)sizeof VendorDescriptorStart || size > HID_MAX_DESCRIPTOR_SIZE)
        return 0;
    struct hidraw_report_descriptor descriptor;
    memset(&descriptor, 0, sizeof descriptor);
    descriptor.size = (uint32_t)size;
    if (ioctl(fd, HIDIOCGRDESC, &descriptor) < 0)
        return 0;
    return memcmp(descriptor.value, VendorDescriptorStart, sizeof VendorDescriptorStart) == 0;
}

static int openVendorInterface(void)
{
    for (int index = 0; index < MaxHidraw; index++) {
        if (!isGalleonKeyboard(index))
            continue;
        char path[32];
        snprintf(path, sizeof path, "/dev/hidraw%d", index);
        const int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            printf("%s: cannot open (%s)\n", path, strerror(errno));
            continue;
        }
        if (hasVendorDescriptor(fd)) {
            printf("using %s\n", path);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void drain(int fd)
{
    uint8_t buffer[256];
    struct pollfd p = {fd, POLLIN, 0};
    while (poll(&p, 1, 0) > 0 && read(fd, buffer, sizeof buffer) > 0)
        ;
}

static int query(int fd, uint8_t property)
{
    uint8_t request[PacketSize];
    memset(request, 0, sizeof request);
    request[1] = 0x01;
    request[3] = CommandGet;
    request[4] = property;

    drain(fd); // never match a stale reply
    if (write(fd, request, sizeof request) != (ssize_t)sizeof request) {
        printf("prop 0x%02x: write failed (%s)\n", property, strerror(errno));
        return -1;
    }

    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, TimeoutMs) <= 0) {
        printf("prop 0x%02x: no reply in %d ms\n", property, TimeoutMs);
        return -1;
    }
    uint8_t reply[256];
    const ssize_t n = read(fd, reply, sizeof reply);
    if (n < 9) {
        printf("prop 0x%02x: short reply (%zd bytes)\n", property, n);
        return -1;
    }
    printf("prop 0x%02x: raw", property);
    for (int i = 0; i < 12 && i < n; i++)
        printf(" %02x", reply[i]);
    if (reply[3] != CommandGet) {
        printf("  (unexpected: byte 3 is not 0x02)\n");
        return -1;
    }
    const uint32_t value = (uint32_t)reply[5] | ((uint32_t)reply[6] << 8) | ((uint32_t)reply[7] << 16) | ((uint32_t)reply[8] << 24);
    printf("  status=%u value=0x%x (%u)", reply[4], value, value);
    if (property == 0x13 && reply[4] == 0)
        printf("  firmware %u.%u.%u", reply[5], reply[6], reply[7] | (reply[8] << 8));
    printf("\n");
    return 0;
}

int main(void)
{
    const int fd = openVendorInterface();
    if (fd < 0) {
        puts("vendor interface of 1b1c:2b0c not found or not accessible");
        return 1;
    }
    int failures = 0;
    for (size_t i = 0; i < sizeof Properties; i++)
        failures += query(fd, Properties[i]) != 0;
    close(fd);
    return failures ? 1 : 0;
}
