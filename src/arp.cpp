#include "arp.h"
#include "ethernet.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <cstring>

// Compara una dirección IP (4 bytes) con un objeto Ipv4Address.
// Retorna true si son iguales, false en caso contrario.
static bool ipEquals(const std::uint8_t* ip, const Ipv4Address& other)
{
	return std::equal(ip, ip + other.size(), other.begin());
}

// Convierte una IP (4 bytes) a texto "a.b.c.d" para logs.
// Recibe un puntero a 4 bytes y retorna un string con el formato dotted decimal.
static std::string ipToString(const std::uint8_t* ip)
{
	// Usamos stringstream para evitar sprintf y manejar tipos de forma segura.
	std::ostringstream oss;
	// Cada byte se imprime como entero decimal separado por puntos.
	oss << static_cast<int>(ip[0]) << '.'
		<< static_cast<int>(ip[1]) << '.'
		<< static_cast<int>(ip[2]) << '.'
		<< static_cast<int>(ip[3]);
	// Devolvemos la cadena ya construida.
	return oss.str();
}

std::vector<std::string> formatArpTable(
	const std::unordered_map<std::uint32_t, ArpEntry>& table,
	std::chrono::steady_clock::time_point now)
{
	std::vector<std::string> lines;
	lines.reserve(table.size() + 1);
	lines.push_back("IP -> MAC (TTL s)");

	for (const auto& it : table) {
		std::uint32_t key = it.first;
		Ipv4Address ip = {
			static_cast<std::uint8_t>((key >> 24) & 0xFF),
			static_cast<std::uint8_t>((key >> 16) & 0xFF),
			static_cast<std::uint8_t>((key >> 8) & 0xFF),
			static_cast<std::uint8_t>(key & 0xFF)
		};
		long ttl = std::chrono::duration_cast<std::chrono::seconds>(it.second.expiresAt - now).count();
		if (ttl < 0) ttl = 0;
		std::string line = ipToString(ip.data()) + " -> " + macToString(it.second.mac) +
			" (" + std::to_string(ttl) + ")" + (it.second.resolved ? "" : " [PEND]");
		lines.push_back(line);
	}

	return lines;
}

// Manejo básico de ARP: solo imprime resumen de Request/Reply.
// Parámetros: header ARP, MAC origen/destino e IPs origen/destino.
// (No responde ni actualiza tabla ARP todavía.)
static void handleArp(const ArpHeader& header,
					 const MacAddress& senderMac,
					 const std::uint8_t* senderIp,
					 const MacAddress& targetMac,
					 const std::uint8_t* targetIp) {
	// Opcode en el header está en big-endian, lo convertimos a host.
	const std::uint16_t opcode = ntohs(header.opcode);
	// Si opcode es 1, es una solicitud ARP.
	if (opcode == 1)
	{
		// Log: quién pregunta por quién (IP/MAC origen y destino).
		printf("ARP Request: %s (%s) -> %s (%s)\n",
				ipToString(senderIp).c_str(), macToString(senderMac).c_str(),
				ipToString(targetIp).c_str(), macToString(targetMac).c_str());
	}
	// 2 = ARP Reply según RFC 826.
	else if (opcode == 2)
	{
		// Log: el emisor anuncia su MAC asociada a su IP.
		printf("ARP Reply: %s is-at %s\n",
				ipToString(senderIp).c_str(), macToString(senderMac).c_str());
	}
	// En caso de otro opcode no reconocido.
	else
	{
		// Cualquier otro opcode no es reconocido en esta versión básica.
		printf("Opcode ARP desconocido: %u\n", static_cast<unsigned>(opcode));
	}
}

std::optional<ArpInfo> parseArpFrame(const EthernetFrame& frame)
{
	if (frame.etherType != EtherType::ARP) return std::nullopt;
	const std::vector<uint8_t>& payload = frame.payload;
	if (payload.size() < sizeof(ArpHeader)) return std::nullopt;

	const ArpHeader* header = reinterpret_cast<const ArpHeader*>(payload.data());
	const std::uint16_t hardwareType = ntohs(header->hardwareType);
	const std::uint16_t protocolType = ntohs(header->protocolType);
	const std::uint16_t opcode = ntohs(header->opcode);
	const std::uint8_t macLen = header->hardwareSize;
	const std::uint8_t ipLen = header->protocolSize;

	if (hardwareType != 1 || protocolType != EtherType::IPv4) return std::nullopt;
	if (macLen != 6 || ipLen != 4) return std::nullopt;

	const size_t offsetSenderMac = sizeof(ArpHeader);
	const size_t offsetSenderIp  = offsetSenderMac + macLen;
	const size_t offsetTargetMac = offsetSenderIp + ipLen;
	const size_t offsetTargetIp  = offsetTargetMac + macLen;
	const size_t minSize = offsetTargetIp + ipLen;
	if (payload.size() < minSize) return std::nullopt;

	ArpInfo info{};
	info.opcode = opcode;
	std::copy_n(payload.data() + offsetSenderMac, 6, info.senderMac.begin());
	std::copy_n(payload.data() + offsetTargetMac, 6, info.targetMac.begin());
	std::copy_n(payload.data() + offsetSenderIp, 4, info.senderIp.begin());
	std::copy_n(payload.data() + offsetTargetIp, 4, info.targetIp.begin());
	return info;
}

// Crea una respuesta ARP Reply si el frame recibido es un ARP Request dirigido a nosotros.
// Retorna std::optional con el frame de respuesta, o std::nullopt si no se puede responder.
std::optional<EthernetFrame> makeArpReply(const EthernetFrame& frame,
									 const MacAddress& myMac,
									 const Ipv4Address& myIp,
									 std::string& outMsg)
{
	auto infoOpt = parseArpFrame(frame);
	if (!infoOpt) return std::nullopt;
	const ArpInfo& info = *infoOpt;
	if (info.opcode != 1) return std::nullopt;
	if (!ipEquals(info.targetIp.data(), myIp)) return std::nullopt;

	EthernetFrame reply;
	reply.dst = info.senderMac;
	reply.src = myMac;
	reply.etherType = EtherType::ARP;

	ArpHeader outHeader{};
	outHeader.hardwareType = htons(1);
	outHeader.protocolType = htons(EtherType::IPv4);
	outHeader.hardwareSize = 6;
	outHeader.protocolSize = 4;
	outHeader.opcode = htons(2);

	reply.payload.resize(sizeof(ArpHeader) + 6 + 4 + 6 + 4);
	std::uint8_t* out = reply.payload.data();
	std::memcpy(out, &outHeader, sizeof(ArpHeader));
	std::memcpy(out + sizeof(ArpHeader), myMac.data(), 6);
	std::memcpy(out + sizeof(ArpHeader) + 6, myIp.data(), 4);
	std::memcpy(out + sizeof(ArpHeader) + 6 + 4, info.senderMac.data(), 6);
	std::memcpy(out + sizeof(ArpHeader) + 6 + 4 + 6, info.senderIp.data(), 4);

	outMsg = "ARP Reply: " + ipToString(myIp.data()) + " is-at " + macToString(myMac);
	return reply;
}

// Construye un ARP Request (who-has) desde nuestra IP/MAC hacia un target IP.
std::optional<EthernetFrame> makeArpRequest(const MacAddress& myMac,
									 const Ipv4Address& myIp,
									 const Ipv4Address& targetIp,
									 std::string& outMsg)
{
	EthernetFrame req;
	// Broadcast para preguntar a todos.
	req.dst = MacAddress{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	// Nuestra MAC como origen.
	req.src = myMac;
	// EtherType ARP.
	req.etherType = EtherType::ARP;

	ArpHeader header{};
	header.hardwareType = htons(1);
	header.protocolType = htons(EtherType::IPv4);
	header.hardwareSize = 6;
	header.protocolSize = 4;
	header.opcode = htons(1); // Request

	req.payload.resize(sizeof(ArpHeader) + 6 + 4 + 6 + 4);
	std::uint8_t* out = req.payload.data();
	std::memcpy(out, &header, sizeof(ArpHeader));
	// Sender MAC/IP
	std::memcpy(out + sizeof(ArpHeader), myMac.data(), 6);
	std::memcpy(out + sizeof(ArpHeader) + 6, myIp.data(), 4);
	// Target MAC (desconocida = 00:00:00:00:00:00)
	std::memset(out + sizeof(ArpHeader) + 6 + 4, 0, 6);
	// Target IP
	std::memcpy(out + sizeof(ArpHeader) + 6 + 4 + 6, targetIp.data(), 4);

	outMsg = "ARP Request: who-has " + ipToString(targetIp.data()) + " tell " + ipToString(myIp.data());
	return req;
}

// Detecta si el frame es ARP, valida tamaños, extrae MAC/IP y llama a handleArp().
void arpDetection(const EthernetFrame& frame) {
	// Si el EtherType no es ARP, no hacemos nada (salimos rápido).
	if (frame.etherType != EtherType::ARP)
	{
		return;
	}
	// Usamos la carga útil del frame como cuerpo ARP.
	const std::vector<uint8_t>& payload = frame.payload;

	// Necesitamos al menos el header fijo de ARP para continuar.
	if (payload.size() < sizeof(ArpHeader)) {
		printf("size error");
		return;
	}

	// Interpretamos los primeros bytes del payload como ArpHeader.
	const ArpHeader *header = reinterpret_cast<const ArpHeader*>(payload.data());
	// Campos de tipo están en big-endian y se convierten a host.
	const std::uint16_t hardwareType = ntohs(header->hardwareType);
	const std::uint16_t protocolType = ntohs(header->protocolType);

	// Leemos longitudes declaradas por el emisor.
	uint8_t macLen = header->hardwareSize; // Debería ser 6 para Ethernet
	uint8_t ipLen  = header->protocolSize; // Debería ser 4 para IPv4
	// Calculamos offsets dentro del payload según el formato ARP.
	size_t offsetSenderMac = sizeof(ArpHeader);
	size_t offsetSenderIp  = offsetSenderMac + macLen;
	size_t offsetTargetMac = offsetSenderIp + ipLen;
	size_t offsetTargetIp  = offsetTargetMac + macLen;
	// Tamaño mínimo requerido para leer todos los campos variables.
	const size_t minSize = offsetTargetIp + ipLen;
	// Validamos que el payload realmente tenga los bytes declarados.
	if (payload.size() < minSize)
	{
		printf("ARP payload incompleto\n");
		return;
	}

	// Aceptamos sólo Ethernet(6) + IPv4(4) en esta versión básica.
	if (macLen != 6 || ipLen != 4)
	{
		printf("ARP con tamaños no soportados: mac=%u ip=%u\n",
				static_cast<unsigned>(macLen), static_cast<unsigned>(ipLen));
		return;
	}

	// Aceptamos sólo hardware Ethernet (1) y protocolo IPv4 (0x0800).
	if (hardwareType != 1 || protocolType != EtherType::IPv4)
	{
		printf("ARP no IPv4/Ethernet (htype=0x%04x ptype=0x%04x)\n",
				hardwareType, protocolType);
		return;
	}

	// Copiamos MAC origen/target a estructuras seguras de 6 bytes.
	MacAddress senderMac{};
	MacAddress targetMac{};
	std::copy_n(payload.data() + offsetSenderMac, 6, senderMac.begin());
	std::copy_n(payload.data() + offsetTargetMac, 6, targetMac.begin());

	// Tomamos punteros a las IPs dentro del payload (4 bytes cada una).
	const uint8_t* senderIpPtr = payload.data() + offsetSenderIp;
	const uint8_t* targetIpPtr = payload.data() + offsetTargetIp;

	// Log mínimo para indicar detección y origen.
	printf("ARP IPv4 detectado. IP Origen: %s\n", ipToString(senderIpPtr).c_str());
	// Delegamos el resumen Request/Reply a handleArp().
	handleArp(*header, senderMac, senderIpPtr, targetMac, targetIpPtr);
}