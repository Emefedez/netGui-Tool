#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include "ethernet.h"

using Ipv4Address = std::array<std::uint8_t, 4>;

#pragma pack(push, 1)
struct ArpHeader { //RECORDAR: big-endian
    std::uint16_t hardwareType; // Ej: 1 (Ethernet)
    std::uint16_t protocolType; // Ej: 0x0800 (IPv4)
    std::uint8_t  hardwareSize; // Longitud dirección física (n)
    std::uint8_t  protocolSize; // Longitud dirección lógica (m)
    std::uint16_t opcode;       // Request/Reply
};
#pragma pack(pop)

// Procesa frames ARP (detección básica)
void arpDetection(const EthernetFrame& frame);

struct ArpInfo {
    std::uint16_t opcode = 0;
    MacAddress senderMac{};
    Ipv4Address senderIp{};
    MacAddress targetMac{};
    Ipv4Address targetIp{};
};

struct ArpEntry {
    MacAddress mac{};
    std::chrono::steady_clock::time_point expiresAt{};
    bool resolved = false;
};

// Extrae campos ARP útiles para tabla (request/reply). Retorna nullopt si no aplica.
std::optional<ArpInfo> parseArpFrame(const EthernetFrame& frame);

// Formatea una tabla ARP en líneas legibles para la UI.
std::vector<std::string> formatArpTable(
    const std::unordered_map<std::uint32_t, ArpEntry>& table,
    std::chrono::steady_clock::time_point now);

// Si el frame es ARP Request para nuestra IP, construye un ARP Reply.
// Retorna nullopt si no aplica.
std::optional<EthernetFrame> makeArpReply(const EthernetFrame& frame,
                                          const MacAddress& myMac,
                                          const Ipv4Address& myIp,
                                          std::string& outMsg);

// Construye un ARP Request (who-has) para el target IP.
// Retorna nullopt si no se puede construir.
std::optional<EthernetFrame> makeArpRequest(const MacAddress& myMac,
                                            const Ipv4Address& myIp,
                                            const Ipv4Address& targetIp,
                                            std::string& outMsg);