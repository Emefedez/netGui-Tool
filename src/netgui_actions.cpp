#include "netgui_actions.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

/**
 * @brief Read a whole text file into a string.
 */
static std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/**
 * @brief Write a string to a file (overwrite).
 */
static bool writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << content;
    return true;
}

EthernetFrame makeDefaultDemoFrame(int modeBit)
{
    EthernetFrame frame;
    frame.dst = MacAddress{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    frame.src = MacAddress{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    frame.etherType = EtherType::Demo;

    const std::uint8_t fill = (modeBit == 0) ? 0x00u : 0xFFu;
    frame.payload.assign(46, fill);

    // Marker so you can see a stable pattern in captures.
    if (!frame.payload.empty())
    {
        frame.payload[0] = 0x42;
        if (frame.payload.size() > 1) frame.payload[1] = static_cast<std::uint8_t>(modeBit);
    }

    return frame;
}

bool ensureCustomPacketTemplate(const std::filesystem::path& packetFile, std::string& outMsg)
{
    if (std::filesystem::exists(packetFile))
    {
        outMsg = "Custom packet file exists: " + packetFile.string();
        return true;
    }

    const std::string templateText =
        "# Custom Ethernet frame bytes (no FCS)\n"
        "# Format: hex bytes separated by spaces/newlines. Comments with # or //.\n"
        "# dst-mac (6)   src-mac (6)   ethertype (2)   payload (...)\n"
        "ff ff ff ff ff ff   02 00 00 00 00 01   88 b5\n"
        "42 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";

    if (!writeTextFile(packetFile, templateText))
    {
        outMsg = "ERROR: Could not create packet file: " + packetFile.string();
        return false;
    }

    outMsg = "Created packet template: " + packetFile.string();
    return true;
}

void openFileInEditor(const std::filesystem::path& file, std::string& outMsg)
{
    // GUI opener first (best UX on desktop).
    const char* sudoUser = std::getenv("SUDO_USER");
    const char* display = std::getenv("DISPLAY");
    const char* xauth = std::getenv("XAUTHORITY");
    std::string cmd;
    if (sudoUser && sudoUser[0] != '\0')
    {
        // When running as root, open as the original desktop user and preserve
        // the GUI-related environment variables.
        cmd = std::string("sudo -u ") + sudoUser + " -E";
        if (display && display[0] != '\0') cmd += std::string(" DISPLAY=\"") + display + "\"";
        if (xauth && xauth[0] != '\0') cmd += std::string(" XAUTHORITY=\"") + xauth + "\"";
        cmd += " xdg-open \"" + file.string() + "\" >/dev/null 2>&1 &";
    }
    else
    {
        cmd = "xdg-open \"" + file.string() + "\" >/dev/null 2>&1 &";
    }

    if (std::system(cmd.c_str()) == 0)
    {
        outMsg = "Opened file: " + file.string();
        return;
    }

    // Terminal editor fallback.
    const char* editor = std::getenv("VISUAL");
    if (!editor || editor[0] == '\0') editor = std::getenv("EDITOR");
    if (!editor || editor[0] == '\0') editor = "nano";

    outMsg = std::string("WARN: xdg-open failed, falling back to ") + editor;
    cmd = std::string(editor) + " \"" + file.string() + "\"";
    (void)std::system(cmd.c_str());
}

std::optional<std::vector<std::uint8_t>> loadCustomPacket(const std::filesystem::path& packetFile)
{
    if (!std::filesystem::exists(packetFile)) return std::nullopt;

    const std::string content = readTextFile(packetFile);
    if (content.empty()) return std::nullopt;

    return parseHexBytesFile(content);
}
