// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Read-only probe: asks each LampArray interface for its attributes report
// through a USB control transfer (HID GET_REPORT, feature). Nothing is written.
// Build: gcc -o lamparray-probe lamparray-probe.c $(pkg-config --cflags --libs libusb-1.0)
#include <libusb-1.0/libusb.h>
#include <stdio.h>
/* Read-only: HID GET_REPORT (feature) of the LampArrayAttributes report. */
static void probe(libusb_context *c, unsigned pid, int itf, int id) {
  libusb_device_handle *h = libusb_open_device_with_vid_pid(c, 0x1b1c, pid);
  if (!h) { printf("%04x: open failed\n", pid); return; }
  int r = libusb_claim_interface(h, itf);
  if (r) { printf("%04x: claim itf %d: %s\n", pid, itf, libusb_error_name(r)); libusb_close(h); return; }
  unsigned char b[64] = {0};
  r = libusb_control_transfer(h, 0xA1, 0x01, (3 << 8) | id, itf, b, 23, 1000);
  printf("%04x itf%d id%d: ret=%d", pid, itf, id, r);
  if (r > 0) { printf(" bytes:"); for (int i = 0; i < r; i++) printf(" %02x", b[i]);
    printf("\n  lamps=%u", b[1] | b[2] << 8); }
  else printf(" (%s)", libusb_error_name(r));
  printf("\n");
  libusb_release_interface(h, itf); libusb_close(h);
}
int main(void) { libusb_context *c; libusb_init(&c);
  probe(c, 0x2b0c, 4, 1); probe(c, 0x2b18, 3, 224); libusb_exit(c); return 0; }
