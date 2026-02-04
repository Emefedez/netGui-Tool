#pragma once

#include "tap.h"

/**
 * @brief Run the netGui application loop.
 *
 * This function owns raylib/raygui initialization and the main draw loop.
 * The caller is responsible for creating/configuring the TAP device.
 */
int runNetGuiApp(TapDevice& tap);
