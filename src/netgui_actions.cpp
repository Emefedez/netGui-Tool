#include "netgui_actions.h"

#include <cstdio>
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
    // Terminal editor, blocking until exit.
    const char* editor = std::getenv("VISUAL");
    if (!editor || editor[0] == '\0') editor = std::getenv("EDITOR");
    if (!editor || editor[0] == '\0') editor = "nano";

    std::string cmd = std::string(editor) + " \"" + file.string() + "\"";
    int ret = std::system(cmd.c_str());
    if (ret == 0) {
        outMsg = "[INFO] Archivo editado: " + file.string();
    } else {
        outMsg = "[WARN] Error al editar con " + std::string(editor);
    }
}

bool saveRxFrameAsCustom(const EthernetFrame& frame, const std::filesystem::path& packetFile, std::string& outMsg)
{
    auto bytes = serializeEthernetII(frame);
    std::string hexLine;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0 && i % 16 == 0) hexLine += "\n";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x ", static_cast<unsigned>(bytes[i]));
        hexLine += buf;
    }
    hexLine += "\n";

    std::ofstream out(packetFile);
    if (!out.is_open()) {
        outMsg = "[WARN] No se pudo guardar custom: " + packetFile.string();
        return false;
    }
    out << "# Capturado desde RX\n";
    out << "# " << describeEthernetII(frame) << "\n";
    out << hexLine;
    out.close();

    outMsg = "[INFO] RX guardado como custom " + std::to_string(bytes.size()) + " bytes";
    return true;
}

std::optional<std::vector<std::uint8_t>> loadCustomPacket(const std::filesystem::path& packetFile)
{
    if (!std::filesystem::exists(packetFile)) return std::nullopt;

    const std::string content = readTextFile(packetFile);
    if (content.empty()) return std::nullopt;

    return parseHexBytesFile(content);
}
