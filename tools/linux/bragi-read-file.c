// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY: opens one file of the keyboard's file system into buffer 0,
// asks for its size, reads it, and unbinds the buffer. Nothing is written,
// created or deleted: the code contains only these four commands.
//
//   0x0d open file   args: buffer index, file ID (2 bytes little-endian)
//   0x09 describe    args: buffer index
//   0x08 read        args: buffer index
//   0x05 unbind      args: count (1), buffer index
//
// Sources: the usbmon capture of the Web Hub (the same four commands, in the
// same order, e.g. `0d 00 26`, `09`, `08`, `05 01`) and the command table read
// from the Web Hub's client, both in docs/protocol/README.md. Frame header:
// 00 01 <session = 00> <command>, 128 bytes, written through hidraw as 129
// bytes (the kernel drops the leading report number 0).
//
// The reply to read has the status in byte 4 and the data from byte 5 (seen
// on the 35-byte file 0x26 and on the 137-byte profile). The reply to describe
// is printed raw, with the 16-bit value at byte 7 as a *candidate* size (that
// is what 0x26 and the profile showed: 0x23 and 0x89), not a documented field.
//
// The content of a file can hold identifiers (file 0x26 holds a 32-character
// device ID): do not post the output as is.
//
// Build: gcc -Wall -o bragi-read-file bragi-read-file.c
// Run:   ./bragi-read-file 0x22        (close OpenRGB, OpenLinkHub and the Web Hub first)

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum { PacketSize = 128, MaxHidraw = 64, TimeoutMs = 500, MaxReads = 6, ReplyShown = 64 };

static const uint8_t CmdOpenFile = 0x0d;
static const uint8_t CmdDescribe = 0x09;
static const uint8_t CmdRead = 0x08;
static const uint8_t CmdUnbind = 0x05;
static const uint8_t VendorDescriptorStart[] = {0x06, 0x42, 0xff, 0x09, 0x01};

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
        if (fd < 0)
            continue;
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

// Writes the 128-byte frame (hidraw wants the report number 0 in front) and
// reads the answer. Returns the answer length, or -1.
static int transact(int fd, const uint8_t *frame, uint8_t *reply, size_t replySize)
{
    uint8_t buffer[PacketSize + 1];
    buffer[0] = 0x00;
    memcpy(buffer + 1, frame, PacketSize);
    drain(fd);
    if (write(fd, buffer, sizeof buffer) != (ssize_t)sizeof buffer) {
        printf("  write failed (%s)\n", strerror(errno));
        return -1;
    }
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, TimeoutMs) <= 0) {
        puts("  no reply");
        return -1;
    }
    const ssize_t n = read(fd, reply, replySize);
    return n < 5 ? -1 : (int)n;
}

static void showReply(const char *what, const uint8_t *reply, int length)
{
    printf("%sstatus=%u, %d bytes, first %d:", what, reply[4], length, length < ReplyShown ? length : ReplyShown);
    for (int i = 0; i < length && i < ReplyShown; i++)
        printf(" %02x", reply[i]);
    printf("\n");
}

// Builds 00 01 00 <command> <args...>, zero-padded to 128 bytes.
static void makeFrame(uint8_t *frame, uint8_t command, const uint8_t *args, size_t argCount)
{
    memset(frame, 0, PacketSize);
    frame[1] = 0x01;
    frame[3] = command;
    memcpy(frame + 4, args, argCount);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        puts("usage: bragi-read-file <file ID, e.g. 0x22>");
        return 2;
    }
    char *end = NULL;
    const unsigned long fileId = strtoul(argv[1], &end, 0);
    if (*end != '\0' || fileId == 0 || fileId > 0xFFFF) {
        puts("file ID must be 1..0xFFFF");
        return 2;
    }

    const int fd = openVendorInterface();
    if (fd < 0) {
        puts("vendor interface of 1b1c:2b0c not found or not accessible");
        return 1;
    }

    uint8_t frame[PacketSize];
    uint8_t reply[256];
    const uint8_t bufferIndex = 0;
    int opened = 0;
    int failed = 0;

    printf("open file 0x%04lx into buffer %u\n", fileId, bufferIndex);
    const uint8_t openArgs[] = {bufferIndex, (uint8_t)(fileId & 0xFF), (uint8_t)(fileId >> 8)};
    makeFrame(frame, CmdOpenFile, openArgs, sizeof openArgs);
    int n = transact(fd, frame, reply, sizeof reply);
    if (n < 0) {
        failed = 1;
    } else {
        showReply("  open: ", reply, n);
        opened = reply[4] == 0;
        if (!opened)
            failed = 1;
    }

    if (opened) {
        makeFrame(frame, CmdDescribe, &bufferIndex, 1);
        n = transact(fd, frame, reply, sizeof reply);
        unsigned candidate = 0;
        if (n < 0) {
            failed = 1;
        } else {
            showReply("  describe: ", reply, n);
            candidate = n > 8 ? (unsigned)(reply[7] | (reply[8] << 8)) : 0;
            printf("  candidate size (16 bits at byte 7): %u\n", candidate);
        }
        for (int i = 0; n >= 0 && i < MaxReads; i++) {
            makeFrame(frame, CmdRead, &bufferIndex, 1);
            n = transact(fd, frame, reply, sizeof reply);
            if (n < 0) {
                failed = 1;
                break;
            }
            char label[24];
            snprintf(label, sizeof label, "  read %d: ", i + 1);
            showReply(label, reply, n);
            if (reply[4] != 0 || n <= 5)
                break;
            // Stop once the candidate size is covered (123 data bytes per read at most).
            if (candidate != 0 && (unsigned)(i + 1) * (PacketSize - 5) >= candidate)
                break;
        }
    }

    // Always unbind the buffer, so the file is left closed.
    const uint8_t unbindArgs[] = {1, bufferIndex};
    makeFrame(frame, CmdUnbind, unbindArgs, sizeof unbindArgs);
    n = transact(fd, frame, reply, sizeof reply);
    if (n >= 0)
        showReply("unbind: ", reply, n);
    else
        failed = 1;

    close(fd);
    return failed ? 1 : 0;
}
