# Project structure (netGui)

This document explains the *codebase structure* (not a database) and how the pieces fit together.

## Top-level

- `CMakeLists.txt` — Builds the `netGui` executable (C++17) and links against system `raylib`.
- `layouts_netGui/` — UI layouts exported by **rGuiLayout** (`.rgl` text format).
  - `Base_Layout.rgl` — Main screen layout.
  - `Ethernet_TAP_configLayout.rgl` — Modal “tools” layout.
- `include/` — Public headers for the project modules.
- `src/` — Implementations.
- `tests/` — Small test(s) for TAP.

## Runtime modules

### App entrypoint

- `src/main.cpp`
  - Creates/opens `tap0` via `TapDevice`.
  - Sets non-blocking mode.
  - Calls `runNetGuiApp(tap)`.

### GUI / main loop

- `include/netgui_gui.h`
- `src/netgui_gui.cpp`
  - Initializes raylib window.
  - Applies GUI defaults + dark theme.
  - Loads `.rgl` layouts and **hot-reloads** them if the files change on disk.
  - Draws:
    - Base layout + embedded log panel (“Terminal output”).
    - Modal tools window (overlay) for send/edit actions.
  - Polls TAP RX in the frame loop and logs received frames.

### Layout loader / renderer (.rgl)

- `include/rgl_layout.h`
- `src/rgl_layout.cpp`
  - Parses `.rgl` lines of the form:
    - `c <id> <type> <name> <x> <y> <w> <h> <anchor_id> <text...>`
  - Supports a subset of raygui controls used by the layouts (WindowBox/Panel/Line/Button/Spinner/StatusBar).
  - Applies a transform before drawing:
    - DPI scale (HiDPI correctness)
    - Uniform fit-to-screen scale (can upscale)
    - Centering
  - Provides:
    - `Layout::draw(UiState&)`
    - `Layout::rectOf(name, outRect)` for custom drawing inside layout panels
    - `Layout::reloadIfChanged()` for hot reload

### TAP device wrapper

- `include/tap.h`
- `src/tap.cpp`
  - Wraps `/dev/net/tun` and creates an `IFF_TAP | IFF_NO_PI` interface.
  - Provides non-blocking reads and raw writes of Ethernet frames.

### Ethernet helpers

- `include/ethernet.h`
- `src/ethernet.cpp`
  - Ethernet II frame serialize/parse helpers.
  - “Describe” helper for readable logging.
  - Parsing of a user editable hex-bytes file for custom packet TX.

### Actions (non-GUI)

- `include/netgui_actions.h`
- `src/netgui_actions.cpp`
  - Builds a default demo Ethernet frame.
  - Ensures a template custom packet file exists.
  - Opens the packet file in an editor (handles sudo/display environment best-effort).
  - Loads custom packet bytes from disk.

## Build + run notes

- Build:
  - `cmake -S . -B build`
  - `cmake --build build -j`
- Run requires a GUI session:
  - `DISPLAY` (X11) or `WAYLAND_DISPLAY` must be set.
- TAP permissions (recommended: no sudo):
  - `sudo ip tuntap add dev tap0 mode tap user $USER`
  - `sudo ip link set dev tap0 up`

## UI editing workflow

- Edit `.rgl` files in `layouts_netGui/` while the app is running.
- On save, the app reloads the layout and logs `Reloaded: ...` inside the GUI log panel.

If you want *full anchor support* from rGuiLayout (the `a ...` lines), we can extend the `.rgl` runtime to apply anchors instead of ignoring them.
