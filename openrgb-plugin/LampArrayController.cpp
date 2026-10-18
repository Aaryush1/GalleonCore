// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LampArrayController.h"

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

// The real layout of the lamps is in the measured positions, not yet in an
// OpenRGB matrix map: the zone is a plain linear one and claims no grid shape.
zone makeZone(const std::string &name, unsigned int count)
{
    zone z;
    z.name = name;
    z.type = ZONE_TYPE_LINEAR;
    z.leds_count = count;
    z.leds_min = count;
    z.leds_max = count;
    return z;
}

} // namespace

RGBController_Setup makeLampArraySetup(const LampArrayData &data,
                                       const std::string &name,
                                       const std::string &location,
                                       void *owner)
{
    const auto count = static_cast<unsigned int>(data.lamps.size());

    RGBController_Setup setup{};
    setup.name = name;
    setup.vendor = "Corsair";
    setup.description = "Galleon 100 SD LampArray (read-only prototype)";
    setup.version = "0";
    setup.serial = location;
    setup.location = location;
    setup.active_mode = 0;
    setup.flags = CONTROLLER_FLAG_VIRTUAL;
    setup.type = DEVICE_TYPE_KEYBOARD;
    setup.modes.push_back(makeDirectMode());
    setup.zones.push_back(makeZone("Lamps", count));
    setup.leds.reserve(count);
    for (const Lamp &lamp : data.lamps) {
        led l;
        l.name = "Lamp " + std::to_string(lamp.id);
        setup.leds.push_back(l);
    }
    setup.object_ptr = owner;
    return setup;
}

} // namespace galleon::openrgb
