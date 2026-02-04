#include "netgui_gui.h"

#include "ethernet.h"
#include "netgui_actions.h"
#include "rgl_layout.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "raylib.h"

// This must be defined exactly once in the whole project.
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

/**
 * @brief In-memory terminal/log buffer rendered in the GUI.
 */
class LogBuffer {
public:
    /**
     * @brief Add one line to the log.
     */
    void push(std::string line)
    {
        if (line.empty()) return;
        if (lines_.size() >= maxLines_) lines_.erase(lines_.begin());
        lines_.push_back(std::move(line));
    }

    /**
     * @brief Draw the log inside a rectangle.
     */
    void draw(const Rectangle& bounds, int scrollLines) const
    {
        const Vector2 dpi = GetWindowScaleDPI();
        const float scale = (dpi.x > 0.0f) ? dpi.x : 1.0f;

        const int fontSize = std::max(10, static_cast<int>(10 * scale));
        const int lineHeight = fontSize + 2;
        const int maxVisible = (lineHeight > 0) ? static_cast<int>(bounds.height) / lineHeight : 0;
        const int newestStart = std::max(0, static_cast<int>(lines_.size()) - std::max(1, maxVisible));
        const int start = std::max(0, newestStart - std::max(0, scrollLines));

        const Color textColor = GetColor(GuiGetStyle(DEFAULT, TEXT_COLOR_NORMAL));

        BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));

        int y = static_cast<int>(bounds.y) + 40;
        const int end = std::min(static_cast<int>(lines_.size()), start + std::max(1, maxVisible));
        for (int i = start; i < end; i++)
        {
            DrawText(lines_[i].c_str(), static_cast<int>(bounds.x) + 8, y, fontSize, textColor);
            y += lineHeight;
        }

        EndScissorMode();
    }

private:
    std::vector<std::string> lines_;
    std::size_t maxLines_ = 500;
};

/**
 * @brief Locate the layouts directory regardless of current working directory.
 */
static std::filesystem::path findLayoutsDir()
{
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 6; i++)
    {
        const auto marker = dir / "layouts_netGui" / "Base_Layout.rgl";
        if (std::filesystem::exists(marker)) return dir / "layouts_netGui";
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }

    return std::filesystem::path("layouts_netGui");
}

/**
 * @brief Apply a readable default theme.
 */
static void applyGuiDefaults()
{
    // Metrics
    GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
    GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, 18);
    GuiSetStyle(DEFAULT, TEXT_PADDING, 8);

    // Dark theme (base properties propagate to all controls).
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x15181cff);
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x2b313aff);

    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x3a424dff);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x1f242bff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xd7dde7ff);

    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x5aa9e6ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x27303aff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xe7f2ffff);

    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x5aa9e6ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x2c3744ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xe7f2ffff);

    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x2a2f37ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0x1b1f25ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0x6c7786ff);

    GuiSetStyle(STATUSBAR, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    GuiSetStyle(STATUSBAR, TEXT_PADDING, 10);
}

int runNetGuiApp(TapDevice& tap)
{
    // Guard against headless sessions (GLFW has been observed to crash otherwise).
    const char* x11Display = std::getenv("DISPLAY");
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if ((!x11Display || x11Display[0] == '\0') && (!waylandDisplay || waylandDisplay[0] == '\0'))
    {
        std::cerr << "No GUI display detected (DISPLAY/WAYLAND_DISPLAY not set).\n";
        std::cerr << "Run from the VM desktop terminal or use X11 forwarding.\n";
        return 1;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1100, 720, "netGui");
    SetTargetFPS(60);
    applyGuiDefaults();

    rgl::Layout baseLayout;
    rgl::Layout tapLayout;
    rgl::UiState baseUi;
    rgl::UiState tapUi;

    LogBuffer log;
    log.push("TAP: opened " + tap.name());
    log.push("Tip: to run without sudo, create tap0 owned by your user:");
    log.push("  sudo ip tuntap add dev tap0 mode tap user $USER");
    log.push("  sudo ip link set dev tap0 up");

    const auto layoutsDir = findLayoutsDir();
    const bool baseOk = baseLayout.loadFromFile((layoutsDir / "Base_Layout.rgl").string());
    const bool tapOk = tapLayout.loadFromFile((layoutsDir / "Ethernet_TAP_configLayout.rgl").string());

    log.push("Layouts dir: " + layoutsDir.string());
    log.push("Base layout: " + baseLayout.filePath());
    log.push("TAP layout:  " + tapLayout.filePath());

    if (!baseOk) log.push("ERROR: failed to load Base_Layout.rgl");
    if (!tapOk) log.push("ERROR: failed to load Ethernet_TAP_configLayout.rgl");

    bool tapToolsOpen = false;
    const std::filesystem::path packetFile = layoutsDir / "custom_packet.hex";

    int logScrollLines = 0;

    std::uint64_t rxCount = 0;
    std::uint8_t buffer[2048];

    while (!WindowShouldClose())
    {
        // Hot-reload layouts if the .rgl text file changes.
        if (baseLayout.reloadIfChanged()) log.push("Reloaded: " + baseLayout.filePath());
        if (tapLayout.reloadIfChanged()) log.push("Reloaded: " + tapLayout.filePath());

        // --- TAP polling ---
        const int bytesRead = tap.read(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            rxCount++;
            auto frame = parseEthernetII(buffer, static_cast<std::size_t>(bytesRead));
            if (frame) log.push("RX[" + std::to_string(rxCount) + "] " + describeEthernetII(*frame));
            else log.push("RX[" + std::to_string(rxCount) + "] " + std::to_string(bytesRead) + " bytes");
        }
        else if (bytesRead < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                log.push(std::string("ERROR: TAP read failed: ") + std::strerror(errno));
                break;
            }
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        // Base layout
        const int mode = baseUi.intValue["Spinner003"];
        baseUi.text["StatusBar002"] = std::string("Mode: ") + std::to_string(mode) + " (0=00, 1=FF)";
        baseUi.text["Button005"] = "TAP Tools";
        baseUi.text["Spinner003"] = "Swap";

        if (baseOk) baseLayout.draw(baseUi);

        Rectangle panelRect{};
        if (baseOk && baseLayout.rectOf("Panel001", panelRect))
        {
            DrawRectangleRec(panelRect, Fade(BLACK, 0.22f));
            // Scroll with mouse wheel when hovering panel.
            if (CheckCollisionPointRec(GetMousePosition(), panelRect))
            {
                const float mw = GetMouseWheelMove();
                if (mw != 0.0f) logScrollLines = std::max(0, logScrollLines + static_cast<int>(-mw * 3));
            }
            log.draw(panelRect, logScrollLines);
        }
        else
        {
            panelRect = Rectangle{20, 120, static_cast<float>(GetScreenWidth() - 40), static_cast<float>(GetScreenHeight() - 140)};
            DrawRectangleRec(panelRect, Fade(BLACK, 0.22f));
            if (CheckCollisionPointRec(GetMousePosition(), panelRect))
            {
                const float mw = GetMouseWheelMove();
                if (mw != 0.0f) logScrollLines = std::max(0, logScrollLines + static_cast<int>(-mw * 3));
            }
            log.draw(panelRect, logScrollLines);
            DrawText("Layout missing: Base_Layout.rgl", 20, 20, 18, RED);
        }

        if ((baseOk && baseLayout.pressed("Button005")) || (!baseOk && IsKeyPressed(KEY_ENTER)))
        {
            tapToolsOpen = true;
            log.push("Opened Ethernet/TAP tools");
        }

        // TAP tools modal
        if (tapToolsOpen)
        {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.35f));

            tapUi.text["WindowBox000"] = std::string("Ethernet/TAP Functions") + (std::filesystem::exists(packetFile) ? " (custom packet file present)" : "");
            tapUi.text["Button001"] = "Send packet";
            tapUi.text["Button002"] = "Edit packet file";
            if (tapOk) tapLayout.draw(tapUi);

            if ((tapOk && tapLayout.pressed("WindowBox000")) || IsKeyPressed(KEY_ESCAPE))
            {
                tapToolsOpen = false;
            }

            if ((tapOk && tapLayout.pressed("Button001")) || (!tapOk && IsKeyPressed(KEY_SPACE)))
            {
                auto custom = loadCustomPacket(packetFile);
                if (custom)
                {
                    const int written = tap.write(custom->data(), custom->size());
                    if (written > 0)
                    {
                        auto frame = parseEthernetII(custom->data(), custom->size());
                        if (frame)
                            log.push("TX custom " + std::to_string(written) + " bytes: " + describeEthernetII(*frame));
                        else
                            log.push("TX custom " + std::to_string(written) + " bytes (unparseable frame)");
                    }
                    else log.push(std::string("ERROR: TX failed: ") + std::strerror(errno));
                }
                else
                {
                    const EthernetFrame f = makeDefaultDemoFrame(mode);
                    auto bytes = serializeEthernetII(f);
                    const int written = tap.write(bytes.data(), bytes.size());
                    if (written > 0) log.push("TX demo " + std::to_string(written) + " bytes: " + describeEthernetII(f));
                    else log.push(std::string("ERROR: TX failed: ") + std::strerror(errno));
                }
            }

            if ((tapOk && tapLayout.pressed("Button002")) || (!tapOk && IsKeyPressed(KEY_E)))
            {
                std::string msg;
                (void)ensureCustomPacketTemplate(packetFile, msg);
                log.push(msg);

                openFileInEditor(packetFile, msg);
                log.push(msg);
            }

            if (!tapOk)
            {
                DrawText("Layout missing: Ethernet_TAP_configLayout.rgl", 20, 44, 14, RAYWHITE);
                DrawText("Keys: SPACE=Send  E=Edit packet  ESC=Close", 20, 64, 14, RAYWHITE);
            }
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
