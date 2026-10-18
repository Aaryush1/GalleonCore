// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY probe of the keyboard's vendor interface (usage page 0xFF42,
// usage 1), Corsair "V2"/Bragi protocol. It sends property GET requests only,
// from a fixed list: nothing that selects a mode, writes a colour or touches
// the lighting is ever sent.
//
// Format, from two sources that agree:
//   - galleon-deck docs/PROTOCOL.md ("Read-only queries"): packets are 128
//     bytes after a zero report number; request  08 02 <prop>;
//     reply  00 02 <status> <value, u32 little-endian>.
//   - OpenRGB Controllers/CorsairPeripheralV2Controller, GetAddress():
//     write command 0x08, GET command 0x02, status at byte 2, value at 3..6.
//   Properties tried (all listed as answering in galleon-deck):
//     0x11 vendor ID, 0x12 product ID, 0x13 firmware, 0x41 layout, 0x02 (1000,
//     probably brightness, unconfirmed).
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

enum { PacketSize = 128, WriteSize = PacketSize + 1, MaxHidraw = 64, TimeoutMs = 500 };

static const uint8_t WriteCommand = 0x08;
static const uint8_t CommandGet = 0x02;
static const uint8_t Properties[] = {0x11, 0x12, 0x13, 0x41, 0x02};
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
    uint8_t request[WriteSize];
    memset(request, 0, sizeof request);
    request[0] = 0x00; // report number
    request[1] = WriteCommand;
    request[2] = CommandGet;
    request[3] = property;

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
    if (n < 7) {
        printf("prop 0x%02x: short reply (%zd bytes)\n", property, n);
        return -1;
    }
    printf("prop 0x%02x: raw", property);
    for (int i = 0; i < 12 && i < n; i++)
        printf(" %02x", reply[i]);
    if (reply[1] != CommandGet) {
        printf("  (unexpected: byte 1 is not 0x02)\n");
        return -1;
    }
    const uint32_t value = (uint32_t)reply[3] | ((uint32_t)reply[4] << 8) | ((uint32_t)reply[5] << 16) | ((uint32_t)reply[6] << 24);
    printf("  status=%u value=0x%x (%u)\n", reply[2], value, value);
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
