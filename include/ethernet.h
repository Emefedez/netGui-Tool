
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief 6-byte Ethernet MAC address.
 */
using MacAddress = std::array<std::uint8_t, 6>;

/**
 * @brief Common EtherType values.
 */
namespace EtherType {
static constexpr std::uint16_t IPv4 = 0x0800;
static constexpr std::uint16_t ARP = 0x0806;
static constexpr std::uint16_t IPv6 = 0x86DD;

/**
 * @brief Experimental EtherType used by this demo.
 *
 * Using an unassigned/experimental value reduces the chance that the host OS
 * tries to interpret the frame as a real protocol.
 */
static constexpr std::uint16_t Demo = 0x88B5;
}  // namespace EtherType

/**
 * @brief Ethernet II frame (without FCS).
 *
 * TAP devices expose L2 frames without the trailing CRC/FCS.
 */
struct EthernetFrame {
	MacAddress dst{};
	MacAddress src{};
	std::uint16_t etherType = EtherType::Demo;
	std::vector<std::uint8_t> payload;
};

/**
 * @brief Convert a MAC address to a canonical string ("aa:bb:cc:dd:ee:ff").
 */
std::string macToString(const MacAddress& mac);

/**
 * @brief Parse a MAC address from common representations.
 *
 * Accepts "aa:bb:cc:dd:ee:ff" or "aabbccddeeff".
 */
std::optional<MacAddress> parseMac(std::string_view text);

/**
 * @brief Serialize an Ethernet II frame into bytes suitable for TAP write().
 *
 * The output has minimum payload padding applied (Ethernet minimum frame size
 * without FCS is 60 bytes; header is 14 bytes => minimum payload is 46 bytes).
 */
std::vector<std::uint8_t> serializeEthernetII(const EthernetFrame& frame);

/**
 * @brief Parse an Ethernet II frame from raw bytes.
 * @return Parsed frame if the buffer is large enough.
 */
std::optional<EthernetFrame> parseEthernetII(const std::uint8_t* data, std::size_t size);

/**
 * @brief Return a short human-readable summary for debugging/logging.
 */
std::string describeEthernetII(const EthernetFrame& frame);

/**
 * @brief Convert a byte buffer to a compact hex string.
 *
 * Designed for logs and debugging (not a full hexdump UI).
 */
std::string toHex(const std::uint8_t* data, std::size_t size, std::size_t maxBytes = 256);

/**
 * @brief Convenience overload for vectors.
 */
inline std::string toHex(const std::vector<std::uint8_t>& bytes, std::size_t maxBytes = 256) {
	return toHex(bytes.data(), bytes.size(), maxBytes);
}

/**
 * @brief Parse a loose "hex bytes" file into raw bytes.
 *
 * Accepts any whitespace separators. Lines may contain comments starting with
 * '#' or '//'. Example:
 *   ff ff ff ff ff ff 02 00 00 00 00 01 88 b5 00 01 02
 */
std::optional<std::vector<std::uint8_t>> parseHexBytesFile(const std::string& fileContent);

