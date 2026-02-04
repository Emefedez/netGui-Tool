#include "ethernet.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

/**
 * @brief Convert a MAC address to "aa:bb:cc:dd:ee:ff".
 */
std::string macToString(const MacAddress& mac)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < mac.size(); i++)
    {
        if (i) oss << ':';
        oss << std::setw(2) << static_cast<int>(mac[i]);
    }
    return oss.str();
}

/**
 * @brief True if the character is a hexadecimal digit.
 */
static bool isHexDigit(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

/**
 * @brief Parse a MAC address from a string.
 */
/**
 * @brief Parse a MAC address from a string (supports "aa:bb:cc:dd:ee:ff" or "aabbccddeeff").
 */
std::optional<MacAddress> parseMac(std::string_view text)
{
    // Convert to string and remove all whitespace
    std::string s(text);
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), s.end());

    MacAddress mac{};

    if (s.size() == 17)
    {
        // Parse colon-separated format: aa:bb:cc:dd:ee:ff
        int outIndex = 0;
        for (std::size_t i = 0; i < s.size();)
        {

            /*        
            Looking at the colon-separated MAC address parsing (the 17-character format `aa:bb:cc:dd:ee:ff`):

            - **`6`** – A MAC address has exactly 6 bytes (12/6 = 2 hex digits per byte)
            - **`17`** – The string length: 6 bytes × 2 hex digits + 5 colons = `12 + 5 = 17` characters
            - **`2`** – Each byte is represented by 2 hexadecimal digits
            - **`12`** – The continuous format (no colons): 6 bytes × 2 hex digits = 12 characters

            The validation checks ensure:
            1. `outIndex >= 6` – Prevents writing beyond the 6-byte MAC address array
            2. `i + 1 >= s.size()` – Ensures at least 2 characters remain to read (current hex digit + next hex digit)
            3. `isHexDigit()` calls – Validates both characters are valid hexadecimal (0-9, a-f, A-F)

            These are standard constraints for parsing a 48-bit MAC address (6 octets).
            */
            // Ensure we don't write past 6 bytes
            if (outIndex >= 6) return std::nullopt;
            // Ensure at least 2 hex digits remain
            if (i + 1 >= s.size()) return std::nullopt;
            // Validate both characters are hex digits
            if (!isHexDigit(s[i]) || !isHexDigit(s[i + 1])) return std::nullopt;
            
            // Convert 2-character hex string to a byte value
            unsigned int byte = 0;
            std::stringstream ss;
            ss << std::hex << s.substr(i, 2);
            ss >> byte;
            mac[outIndex++] = static_cast<std::uint8_t>(byte & 0xFFu);
            i += 2;
            
            // Expect a colon separator between pairs (except after the last pair)
            if (i < s.size())
            {
                if (s[i] != ':') return std::nullopt;
                i++;
            }
        }
        return mac;
    }

    if (s.size() == 12)
    {
        // Parse continuous hex format: aabbccddeeff (no separators)
        for (int outIndex = 0; outIndex < 6; outIndex++)
        {
            // Extract two hex digits for each byte
            const char h0 = s[outIndex * 2 + 0];
            const char h1 = s[outIndex * 2 + 1];
            // Validate both are hex digits
            if (!isHexDigit(h0) || !isHexDigit(h1)) return std::nullopt;
            
            // Convert 2-character hex pair to a byte value
            unsigned int byte = 0;
            std::stringstream ss;
            ss << std::hex << s.substr(outIndex * 2, 2);
            ss >> byte;
            mac[outIndex] = static_cast<std::uint8_t>(byte & 0xFFu);
        }
        return mac;
    }

    // Unsupported format (wrong length)
    return std::nullopt;
}

/**
 * @brief Serialize an Ethernet II frame (adds minimum-size padding).
 */
std::vector<std::uint8_t> serializeEthernetII(const EthernetFrame& frame)
{
    std::vector<std::uint8_t> out;
    out.reserve(14 + frame.payload.size());

    out.insert(out.end(), frame.dst.begin(), frame.dst.end());
    out.insert(out.end(), frame.src.begin(), frame.src.end());

    out.push_back(static_cast<std::uint8_t>((frame.etherType >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(frame.etherType & 0xFFu));

    out.insert(out.end(), frame.payload.begin(), frame.payload.end());

    // Ethernet minimum is 60 bytes (without FCS). Header is 14 bytes.
    const std::size_t minPayload = 46;
    if (frame.payload.size() < minPayload)
    {
        out.resize(14 + minPayload, 0);
    }

    return out;
}

/**
 * @brief Parse Ethernet II header fields from a raw buffer.
 */
std::optional<EthernetFrame> parseEthernetII(const std::uint8_t* data, std::size_t size)
{
    if (!data || size < 14) return std::nullopt;

    EthernetFrame frame;
    std::copy_n(data + 0, 6, frame.dst.begin());
    std::copy_n(data + 6, 6, frame.src.begin());
    frame.etherType = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[12]) << 8) | data[13]);

    frame.payload.assign(data + 14, data + size);
    return frame;
}

/**
 * @brief Produce a compact text description of a frame.
 */
std::string describeEthernetII(const EthernetFrame& frame)
{
    std::ostringstream oss;
    oss << macToString(frame.src) << " -> " << macToString(frame.dst);
    oss << " type=0x" << std::hex << std::setw(4) << std::setfill('0') << frame.etherType;
    oss << std::dec << " payload=" << frame.payload.size() << "B";
    return oss.str();
}

/**
 * @brief Convert bytes to a compact hex string (truncates after maxBytes).
 */
std::string toHex(const std::uint8_t* data, std::size_t size, std::size_t maxBytes)
{
    if (!data || size == 0) return {};

    const std::size_t n = std::min(size, maxBytes);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < n; i++)
    {
        if (i) oss << ' ';
        oss << std::setw(2) << static_cast<int>(data[i]);
    }

    if (n < size) oss << " ... (" << (size - n) << " more bytes)";
    return oss.str();
}

/**
 * @brief Strip inline comments (# or //) from a line.
 */
static std::string stripComments(const std::string& line)
{
    std::size_t cut = std::string::npos;
    const auto hash = line.find('#');
    if (hash != std::string::npos) cut = std::min(cut, hash);

    const auto slashslash = line.find("//");
    if (slashslash != std::string::npos) cut = std::min(cut, slashslash);

    if (cut == std::string::npos) return line;
    return line.substr(0, cut);
}

/**
 * @brief Parse a "hex bytes" text file into raw bytes.
 */
std::optional<std::vector<std::uint8_t>> parseHexBytesFile(const std::string& fileContent)
{
    std::vector<std::uint8_t> bytes;

    std::istringstream in(fileContent);
    std::string line;
    while (std::getline(in, line))
    {
        line = stripComments(line);
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok)
        {
            if (tok.rfind("0x", 0) == 0 || tok.rfind("0X", 0) == 0) tok = tok.substr(2);
            if (tok.empty()) continue;

            // Allow tokens like "ff," by stripping trailing punctuation.
            while (!tok.empty() && !isHexDigit(tok.back())) tok.pop_back();
            while (!tok.empty() && !isHexDigit(tok.front())) tok.erase(tok.begin());
            if (tok.empty()) continue;

            if (tok.size() != 2)
            {
                // Support aabbcc... groups by splitting into pairs.
                if (tok.size() % 2 != 0) return std::nullopt;
                for (std::size_t i = 0; i < tok.size(); i += 2)
                {
                    const std::string byteStr = tok.substr(i, 2);
                    if (!isHexDigit(byteStr[0]) || !isHexDigit(byteStr[1])) return std::nullopt;
                    unsigned int v = 0;
                    std::stringstream ss;
                    ss << std::hex << byteStr;
                    ss >> v;
                    bytes.push_back(static_cast<std::uint8_t>(v & 0xFFu));
                }
                continue;
            }

            if (!isHexDigit(tok[0]) || !isHexDigit(tok[1])) return std::nullopt;
            unsigned int v = 0;
            std::stringstream ss;
            ss << std::hex << tok;
            ss >> v;
            bytes.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        }
    }

    if (bytes.empty()) return std::nullopt;
    return bytes;
}
