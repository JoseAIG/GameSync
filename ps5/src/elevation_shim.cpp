/*
 * GameSync PS5 client - C entry point for the Lapy elevation client.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * As in PSXS5 / SwanStationPS5 by darkxex (src/platform/elevation_shim.cpp,
 * GPL-3.0-or-later): when no HEN answers the jailbreak request, the packaged
 * Lapy helper (/app0/lapy.elf) is sent to the console's ELF loader on port
 * 9021 and grants this title /data.
 */
#if defined(__PROSPERO__)
#include "elevation.hpp"

extern "C" int gamesync_elevate(const char **route)
{
    const auto status = elevation::request(elevation::Capability::filesystem);
    if (route)
        *route = elevation::path();
    return static_cast<int>(status);
}
#endif
