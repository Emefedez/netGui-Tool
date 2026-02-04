#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ethernet.h"

/**
 * @brief Build a minimal demo Ethernet frame for TAP testing.
 *
 * The payload is padded to the minimum Ethernet size (without FCS).
 * - modeBit=0 => payload filled with 0x00 (except marker)
 * - modeBit=1 => payload filled with 0xFF (except marker)
 */
EthernetFrame makeDefaultDemoFrame(int modeBit);

/**
 * @brief Ensure a template custom packet file exists.
 * @return true if the file exists after the call.
 */
bool ensureCustomPacketTemplate(const std::filesystem::path& packetFile, std::string& outMsg);

/**
 * @brief Open a file in an editor and wait for it to close.
 *
 * Blocks until the editor exits. Uses $VISUAL/$EDITOR or defaults to nano.
 */
void openFileInEditor(const std::filesystem::path& file, std::string& outMsg);

/**
 * @brief Save a received Ethernet frame to the custom packet file (as hex).
 */
bool saveRxFrameAsCustom(const EthernetFrame& frame, const std::filesystem::path& packetFile, std::string& outMsg);

/**
 * @brief Try to parse raw bytes from the custom packet file.
 * @return Parsed bytes if the file exists and contains valid hex.
 */
std::optional<std::vector<std::uint8_t>> loadCustomPacket(const std::filesystem::path& packetFile);
