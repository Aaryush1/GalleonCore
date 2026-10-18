// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeController.h"

#include "FakeKeys.h"

namespace galleon::openrgb {

namespace {

mode makeDirectMode()
{
    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    return direct;
}

zone makeKeysZone()
{
    zone keys;
    keys.name = "Stream Deck keys (fake)";
    keys.type = ZONE_TYPE_LINEAR;
    keys.leds_count = FakeLedCount;
    keys.leds_min = FakeLedCount;
    keys.leds_max = FakeLedCount;
    return keys;
}

std::vector<led> makeLeds()
{
    std::vector<led> leds;
    leds.reserve(FakeLedCount);
    for (unsigned int i = 0; i < FakeLedCount; ++i) {
        led key;
        key.name = fakeLedName(i);
        leds.push_back(key);
    }
    return leds;
}

} // namespace

RGBController_Setup makeFakeSetup(void *owner)
{
    RGBController_Setup setup{};
    setup.name = "Galleon 100 SD (fake)";
    setup.vendor = "GalleonCore";
    setup.description = "Fake 12-LED device proving the plugin registration path";
    setup.version = "0";
    setup.serial = "fake-0001";
    setup.location = "galleoncore:fake";
    setup.active_mode = 0;
    setup.flags = CONTROLLER_FLAG_VIRTUAL;
    setup.type = DEVICE_TYPE_KEYBOARD;
    setup.modes.push_back(makeDirectMode());
    setup.zones.push_back(makeKeysZone());
    setup.leds = makeLeds();
    setup.object_ptr = owner;
    return setup;
}

} // namespace galleon::openrgb
