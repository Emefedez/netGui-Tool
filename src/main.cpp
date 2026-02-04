#include "netgui_gui.h"
#include "tap.h"

#include <exception>
#include <iostream>

/**
 * @brief Program entry point.
 *
 * Keeps the entry small: TAP init + delegate to GUI loop.
 */
int main()
{
    try
    {
        TapDevice tap("tap0");
        tap.setNonBlocking(true);
        return runNetGuiApp(tap);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to initialize TAP: " << e.what() << "\n";
        std::cerr << "Tip: create the device first, assigning ownership: \n";
        std::cerr << "  sudo ip tuntap add dev tap0 mode tap user $USER\n";
        std::cerr << "  sudo ip link set dev tap0 up\n";
        return 1;
    }
}