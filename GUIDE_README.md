NetGui-Tool — Guía de implementación de protocolos (pasos)
=========================================================

Objetivo
--------
Esta guía describe, a alto nivel y en pasos prácticos, cómo implementar sobre la base existente (TAP + `ethernet.cpp`) los protocolos:

- ARP
- ICMP (Echo Request / Reply)
- IPv4
- TCP (mínimo para funcionar)
- HTTP (servidor simple / cliente)
- DHCP (opcional)

No se incluyen implementaciones completas aquí; sólo una hoja de ruta con estructuras, puntos de integración, pruebas y ejemplos de comandos.

Requisitos previos
------------------
- La interfaz TAP (`TapDevice`) funcionando y accesible.
- `ethernet.cpp` con `parseEthernetII()` y `serializeEthernetII()` para convertir wire <-> estructuras.
- Un bucle de I/O que lee desde TAP y entrega buffers a tu pipeline de parsing (actualmente en la TUI).

Notas generales de diseño
-------------------------
- Diseñar cada capa como módulos separados con API clara:
  - L2: `Ethernet` (ya existente)
  - L3: `IPv4` (parse/serialize, checksum)
  - L2.5: `ARP` (resolve/answer)
  - L3.5: `ICMP` (tipo/código + checksum)
  - L4: `TCP` (estado mínimo: LISTEN, SYN-SENT, SYN-RECV, ESTABLISHED; ventanas mínimas)
  - App: `HTTP` (usar TCP sockets implementados por tu stack)

- Mantén funciones puras para parseo/serialización y callbacks/handlers separados para side-effects (p. ej. enviar vía `tap.write`).
- Validación y checksums: implementar verificación RFC: IP checksum, ICMP checksum, TCP checksum (opcional en pruebas locales pero recomendable).

Flujo general de packet processing
---------------------------------
1. Leer bytes desde TAP.
2. `parseEthernetII()` → `EthernetFrame`.
3. Si `etherType == IPv4` → pasar a `parseIPv4()`; si `etherType == ARP` → `parseARP()`.
4. En L3/L4, decidir entrega: ICMP/TCP/UDP/otros. Para TCP, actualizar estado de conexión y generar paquetes de respuesta.
5. Las respuestas pasarán por `serializeIPv4()` / `serializeEthernetII()` y `tap.write()`.

Implementación por protocolo (resumen y pasos)
--------------------------------------------

1) ARP
-------
- Objetivo: responder a ARP Request locales y resolver MAC para envíos IPv4.
- Estructuras: ARP header (HTYPE, PTYPE, HLEN, PLEN, OPER, SHA, SPA, THA, TPA).
- Pasos:
  1. Añadir `parseARP(const uint8_t*, size_t) -> optional<ARPFrame>` y `serializeARP(const ARPFrame&)`.
  2. Mantener una tabla ARP simple (IP -> MAC, TTL).
  3. On ARP request dirigidos a nuestra IP: construir ARP reply (swap SHA/THA, set op=2) y enviar.
  4. On ARP reply: actualizar ARP table.
  5. Cuando quieras enviar IPv4 y no tengas MAC: enviar ARP request y encolar el paquete IPv4 hasta resolución (o enviar ICMP unreachable tras timeout).

- Pruebas:
  - `arping -I tap0 -c 1 <target>` y observar en la TUI y tablas.

2) ICMP (Echo Request/Reply)
----------------------------
- Objetivo: responder a Echo Requests dirigidos a la IP del TAP y poder enviar Echo Requests (tests).
- Estructuras: ICMP header (type, code, checksum, identifier, sequence) + payload.
- Pasos:
  1. Implementar `parseICMP()` y `serializeICMP()` con cálculo/checksum.
  2. En `parseIPv4()`, cuando proto == 1 (ICMP), dispatch a ICMP handler.
  3. Si recibes Echo Request y destino es nuestra IP: construir Echo Reply (swap src/dst IP, set type=0) y enviar.
  4. Para pruebas, implementar función de envío de Echo Request y esperar Reply (mantener mapping id/seq para correlacionar).

- Pruebas:
  - `ping -I tap0 <our-ip>` desde otra terminal.

3) IPv4
--------
- Objetivo: parsing/serialización de la cabecera IPv4, manejo de fragmentación mínimo (puede omitirse inicialmente), verificación de checksum.
- Estructuras: IPv4 header fields, funciones `computeIPv4Checksum()`.
- Pasos:
  1. Implementar `parseIPv4(const uint8_t*, size_t) -> optional<IPv4Packet>` que valide versión=4, header length, total length, checksum.
  2. `serializeIPv4(const IPv4Packet&)` con recalculo de checksum.
  3. Entregar payloads a ICMP/TCP/UDP según `protocol`.

- Notas:
  - Para comenzar, no implementes fragmentación/reassembly; asume MTU suficiente.

4) TCP (mínimo)
----------------
- Objetivo: permitir establecer conexiones TCP simples y transferir datos entre cliente/servidor para HTTP.
- Alcance mínimo recomendado:
  - Soportar handshake (SYN→SYN+ACK→ACK).
  - Soportar recepción de datos y envío de ACKs.
  - Soportar envío de datos desde stack (respuesta HTTP) y cierre básico (FIN handshake optional).
- Estructuras: TCP header, control block `TCB` por conexión con estado, seq/ack, window.
- Pasos:
  1. Implementar `parseTCP()` / `serializeTCP()` con checksum (si no quieres la complejidad completa, puedes dejar checksum=0 en pruebas locales pero documentarlo).
  2. Añadir un `ConnectionTable` que indexe por tuple (srcIP, srcPort, dstIP, dstPort) → `TCB`.
  3. Implementar manejo de estados: LISTEN (server side) y ESTABLISHED.
  4. Para LISTEN: al recibir SYN crear `TCB` con ISN, responder SYN+ACK.
  5. Al recibir ACK completar handshake y poner en ESTABLISHED.
  6. Manejar recepción de payloads: almacenar en buffer de recepción y enviar ACK (incrementar ack).
  7. Para enviar datos: construir segmento con seq, fragmentar si necesario, enviar y esperar ACK (puedes simplificar sin retransmisiones complejas; una retransmisión fija con timeout es suficiente para pruebas).

- Pruebas:
  - Montar un servidor minimal que escuche en un puerto (p. ej. 8080) y responda con un pequeño payload.
  - Desde otra terminal en la VM/x86 usar `curl --interface tap0 http://<tap-ip>:8080`.

5) HTTP (mínimo)
-----------------
- Objetivo: exponer un handler HTTP simple sobre TCP para validar la pila.
- Alcance mínimo:
  - Manejar métodos `GET` y `POST` simples.
  - No requiere keep-alive avanzado; soportar una petición por conexión está bien para empezar.
- Pasos:
  1. Implementar un pequeño `HttpServer` que registre una callback para rutas.
  2. Al recibir payload TCP que contiene request, parsear hasta doble CRLF, extraer method/path/headers/body.
  3. Generar respuesta HTTP/1.0 o 1.1 simple con headers `Content-Length` y un body corto.
  4. Serializar la respuesta y pasarla al TCP layer para enviar.

- Pruebas:
  - `curl --interface tap0 http://<tap-ip>:8080/` debería devolver tu payload.

6) DHCP (opcional)
-------------------
- Objetivo: opcionalmente implementar servidor/cliente DHCP para asignar IPs al TAP.
- Notas:
  - DHCP usa UDP (68/67) y requiere manejo de broadcast y retransmisiones; es útil pero no obligatorio.

Pruebas integradas y debug
--------------------------
- Usa `tcpdump` para comparar con tus paquetes:
  - `sudo tcpdump -i tap0 -e -xx`.
- Usa `arping`, `ping`, `curl` con `--interface tap0` para validar ARP/ICMP/TCP/HTTP.
- Añade logging en cada capa: raw bytes, parsed headers, checksums ok/fail.

Testing sugerido por pasos
-------------------------
1. Implementa ARP reply → validar con `arping` y que tabla ARP se actualice.
2. Implementa IPv4 parsing + ICMP echo reply → validar `ping -I tap0`.
3. Implementa TCP handshake y ACKs → validar `telnet -I tap0 <ip> 8080` y ver handshake.
4. Implementa HTTP handler → validar `curl --interface tap0`.

Consejos prácticos
------------------
- Itera por capas: ARP → IPv4/ICMP → TCP (handshake) → HTTP.
- Mantén tests automáticos (unit tests) para checksums y parsers.
- Evita replicar lógica del kernel: tu stack puede ser minimalista y orientada a pruebas.

Archivos recomendados (estructura)
---------------------------------
- `src/arp.*` : parse/serialize + table
- `src/ipv4.*` : parse/serialize + checksum
- `src/icmp.*` : parse/serialize + handlers
- `src/tcp.*` : TCB, parse/serialize, connection table
- `src/http.*` : simple request/response handler

Licencia y contribuciones
-------------------------
Incluye notas de la licencia del proyecto en la raíz si vas a compartir código.

¿Te interesa que genere esta guía en inglés o que convierta los pasos en issues/PR checklist automáticamente? Si quieres, también puedo generar plantillas de tests unitarios y stubs de código para cada módulo.

*** Fin del documento README.md
