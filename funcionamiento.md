# Documentación Técnica Detallada

Este documento detalla el funcionamiento específico de cada variable y función dentro de los módulos principales del proyecto, así como la interacción lógica con el sistema operativo.

## 1. Módulo TAP (`include/tap.h`, `src/tap.cpp`)

La clase `TapDevice` encapsula la interacción de bajo nivel con el sistema operativo Linux para crear interfaces de red virtuales.

### Variables Miembro (Estado Interno)

*   **`int fd`**:
    *   *Tipo*: Descriptor de archivo (File Descriptor).
    *   *Función*: Almacena el identificador numérico devuelto por `open("/dev/net/tun")`. Es el canal de comunicación directo con el kernel. Todas las lecturas y escrituras de paquetes se realizan a través de este entero.
*   **`std::string dev_name`**:
    *   *Tipo*: Cadena de caracteres.
    *   *Función*: Guarda el nombre de la interfaz de red (ej. "tap0", "tap1"). Aunque se solicita un nombre al crear el objeto, el kernel puede asignar uno distinto si el solicitado está en uso; esta variable almacena el nombre final real asignado por el sistema.

### Métodos (Funciones Miembro)

*   **`TapDevice(const std::string& name)` (Constructor)**:
    *   *Acción*: Abre el archivo especial `/dev/net/tun` y configura la interfaz mediante `ioctl` con los flags `IFF_TAP` (modo Ethernet capa 2) y `IFF_NO_PI` (sin cabeceras de metadatos extra).
    *   *Parámetro `name`*: El nombre sugerido para la interfaz.
*   **`~TapDevice()` (Destructor)**:
    *   *Acción*: Cierra limpia y automáticamente el descriptor de archivo `fd` cuando el objeto se destruye, liberando el recurso del sistema.
*   **`void setNonBlocking(bool non_blocking)`**:
    *   *Acción*: Modifica los flags del descriptor de archivo `fd` usando `fcntl`.
    *   *Parámetro `non_blocking`*: Si es `true`, activa `O_NONBLOCK`. Esto hace que las lecturas no detengan la ejecución del programa si no hay datos, retornando error inmediatamente. 
*   **`int read(unsigned char* buffer, size_t size)`**:
    *   *Acción*: Lee bytes crudos desde la interfaz virtual hacia la memoria del programa.
    *   *Parámetros*: Puntero al buffer de destino y tamaño máximo a leer.
    *   *Retorno*: Número de bytes leídos (tamaño de la trama capturada) o -1 si hubo error (o si no había datos en modo no bloqueante).
*   **`int write(unsigned char* buffer, size_t size)`**:
    *   *Acción*: Envía bytes crudos desde la memoria del programa hacia la interfaz virtual (el sistema operativo "recibe" estos datos).
    *   *Retorno*: Número de bytes escritos exitosamente.
*   **`const std::string& name() const`**:
    *   *Acción*: Getter simple que devuelve el nombre real de la interfaz (`dev_name`).

---

## 2. Módulo Ethernet (`include/ethernet.h`, `src/ethernet.cpp`)

Este conjunto de funciones libres y estructuras define cómo se interpretan los datos binarios que entran y salen del TAP.

### Tipos y Estructuras de Datos

*   **`using MacAddress = std::array<std::uint8_t, 6>`**:
    *   Alias para un array fijo de 6 bytes, representando una dirección física HW.
*   **`struct EthernetFrame`**:
    *   Estructura que modela una trama Ethernet II en memoria.
    *   **`MacAddress dst`**: Dirección MAC del destinatario.
    *   **`MacAddress src`**: Dirección MAC del remitente.
    *   **`std::uint16_t etherType`**: Campo de 2 bytes que indica el protocolo encapsulado (ej. 0x0800 para IPv4). Por defecto usa `EtherType::Demo` (0x88B5).
    *   **`std::vector<std::uint8_t> payload`**: Vector dinámico que contiene los datos útiles transportados por la trama.
*   **`namespace EtherType`**:
    *   Constantes estáticas para identificar protocolos: `IPv4` (0x0800), `ARP` (0x0806), `IPv6` (0x86DD) y `Demo` (0x88B5).

### Funciones de Conversión y Utilidad

*   **`std::string macToString(const MacAddress& mac)`**:
    *   *Acción*: Recorre los 6 bytes de la dirección MAC y los formatea como string hexadecimal separado por dos puntos (ej. "aa:bb:cc:dd:ee:ff").
*   **`std::optional<MacAddress> parseMac(std::string_view text)`**:
    *   *Acción*: Intenta convertir un string en una MAC. Soporta formatos con separadores ("00:11:22...") o sin ellos ("001122...").
    *   *Retorno*: Un `std::optional` que contiene la MAC si el parseo fue exitoso, o vacío (`std::nullopt`) si el formato era inválido.
*   **`std::string toHex(const std::uint8_t* data, ...)`**:
    *   *Acción*: Convierte un buffer arbitrario de bytes a una representación string hexadecimal (ej. "0a ff 1b ...") para logs.

### Funciones de Procesamiento de Tramas

*   **`std::vector<std::uint8_t> serializeEthernetII(const EthernetFrame& frame)`**:
    *   *Acción*: Convierte la estructura `EthernetFrame` en una secuencia plana de bytes para enviar a la red.
    *   *Detalle Importante*: Implementa **padding**. Si el `payload` es menor a 46 bytes, rellena con ceros hasta alcanzar el tamaño mínimo de trama Ethernet (60 bytes header incluido), cumpliendo el estándar 802.3.
*   **`std::optional<EthernetFrame> parseEthernetII(const std::uint8_t* data, std::size_t size)`**:
    *   *Acción*: Interpreta un buffer crudo recibido de la red. Extrae los primeros 14 bytes como cabecera (Dst MAC, Src MAC, EtherType) y el resto como Payload.
    *   *Validación*: Si el buffer tiene menos de 14 bytes, retorna `std::nullopt` porque no es una trama válida.
*   **`std::string describeEthernetII(const EthernetFrame& frame)`**:
    *   *Acción*: Genera un resumen legible para humanos de la trama, mostrando "MAC Origen -> MAC Destino, Protocolo, Tamaño Payload". Útil para debugging visual rápido.
*   **`std::optional<std::vector<std::uint8_t>> parseHexBytesFile(const std::string& fileContent)`**:
    *   *Acción*: Lee el contenido de un archivo de texto, ignora comentarios (# o //) y espacios, y convierte los valores hexadecimales textuales en un buffer binario real para inyectar tráfico.

---

## 3. Flujo de Datos y Configuración del Sistema

Para comprender el comportamiento real del programa, es necesario entender su relación con la configuración del sistema operativo.

### Modelo Mental de Entrada/Salida (Direccionalidad)

**CONCEPTO CLAVE**: Los términos RX/TX en este programa se definen desde la **perspectiva del programa**, no del kernel. Esto puede parecer invertido respecto a la terminología de red tradicional, pero sigue la lógica de las operaciones de I/O del programa:

#### TX (Transmit - Escritura desde el programa)
*   **Operación**: `tap.write()` - El programa **escribe** datos al dispositivo TAP.
*   **Vista del Kernel**: El kernel **recibe** estos datos como si hubieran llegado por un cable físico conectado.
*   **Dirección del flujo**: Programa → Kernel (simulando: Red física → Kernel)
*   **Uso práctico**:
    *   Inyectar tráfico simulado para testing
    *   Enviar respuestas falsas (ej: ARP reply spoofing)
    *   Simular un dispositivo de red que "habla" al sistema
    *   Probar cómo el stack TCP/IP del kernel reacciona a paquetes específicos
*   **Ejemplo**: Ejecutas `[s]` o `[c]` en la app → Se muestra `[TX]` en rojo → El kernel procesa ese paquete como "recibido de la red".

#### RX (Receive - Lectura desde el programa)
*   **Operación**: `tap.read()` - El programa **lee** datos del dispositivo TAP.
*   **Vista del Kernel**: El kernel **envía** estos datos hacia la red (pero el TAP los captura antes).
*   **Dirección del flujo**: Kernel → Programa (capturando: Kernel → Red física)
*   **Uso práctico**:
    *   Sniffing del tráfico saliente del sistema
    *   Capturar paquetes generados por aplicaciones (ping, curl, navegador)
    *   Monitorear qué intenta enviar el sistema operativo
    *   Debugging de protocolos de red a nivel de trama
*   **Ejemplo**: Ejecutas `ping 192.168.1.1` desde otra terminal → El kernel genera ICMP → Se muestra `[RX]` en verde → Tu programa captura lo que el kernel quería enviar.

#### Analogía con hardware de red real

Si pensamos en una tarjeta de red Ethernet física:
*   **TX del hardware**: La tarjeta **transmite** bits al cable (sale del ordenador)
*   **RX del hardware**: La tarjeta **recibe** bits del cable (entra al ordenador)

Con el TAP, el programa actúa como "el cable" en el medio:
*   **TX del programa** (`write`): Inyecta datos "como si vinieran del cable" → El kernel los **recibe**
*   **RX del programa** (`read`): Captura datos "que van hacia el cable" → El kernel los está **transmitiendo**

#### Tabla resumen de perspectivas

| Operación | Función API | Programa | Kernel | Terminología de red tradicional |
|-----------|------------|----------|--------|----------------------------------|
| TX        | `write()`  | Envía    | Recibe | RX (desde perspectiva del kernel) |
| RX        | `read()`   | Recibe   | Envía  | TX (desde perspectiva del kernel) |

**Nota importante**: Al usar `[x]` (Guardar RX como custom), estás guardando un paquete que el kernel generó. Luego, si lo reenvías con `[c]`, lo estás inyectando de vuelta al kernel como si fuera nuevo tráfico entrante.

### Configuración del SO (Prerrequisitos)
El código asume que la interfaz virtual ya ha sido configurada externamente. Sin estos pasos, `tap.read` no recibirá nada y `tap.write` enviará paquetes a la nada:

*   **Creación**: `ip tuntap add dev tap0 mode tap` (Crea la tubería).
*   **Enlace (Link Up)**: `ip link set up dev tap0` (Equivalente a conectar el cable).
*   **Direccionamiento (Opcional)**: `ip addr add 192.168.X.X/24 dev tap0` (Necesario para que el Kernel responda a protocolos IP/ICMP).

---

## 4. Interfaz TUI (ncurses)

La aplicación usa una interfaz en terminal con paneles múltiples y adaptables según el tamaño de la terminal.

### Layout Dinámico y Paneles

La interfaz se adapta automáticamente al tamaño de la terminal:

#### Configuración Básica (Terminal pequeña: <100 columnas)
```
┌─────────────────── Header ───────────────────┐
│ NetGui-Tool (TUI ncurses)                    │
│ Interfaz: tap0 | Estado: ...                 │
└──────────────────────────────────────────────┘
┌─────────────────── Log ──────────────────────┐
│ [RX] aa:bb:cc... -> ff:ff:ff... proto=ARP    │
│ [TX] Demo 0x00 (60B) -> TX OK                │
│ ...                                          │
└──────────────────────────────────────────────┘
┌────────────────── Footer ────────────────────┐
│ [s][d][t][c][e][r][x][b][i][q]               │
└──────────────────────────────────────────────┘
```

#### Configuración Media (100-150 columnas)
```
┌─────────── Header ─────────────┐┌── Último RX ──┐
│ NetGui-Tool                    ││ Dst: ff:ff:... │
└────────────────────────────────┘│ Src: aa:bb:... │
┌────────────── Log ─────────────┐│ Tipo: 0x0806   │
│ [RX] paquete...                ││ Payload:       │
│ [TX] enviado...                ││ 0000: 00 01... │
│ ...                            ││ ...            │
└────────────────────────────────┘└────────────────┘
```

#### Configuración Completa (>160 columnas, >30 líneas)
```
┌──────────────────────────── Header ────────────────────────────┐
│ NetGui-Tool (TUI) - tap0 - Estado: Listo                       │
└────────────────────────────────────────────────────────────────┘
┌───── Desglose Trama TX/RX (compacto) ───────────────────────────┐
│ Dst: FF FF FF FF FF FF | ff:ff:ff:ff:ff:ff                     │
│ Src: 02 00 00 00 00 01 | 02:00:00:00:00:01                     │
│ Type: 88 B5 | 0x88B5 (Demo)                                    │
│ Payload: 42 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...  │
│ Total: 60 bytes (14 header + 46 payload)                       │
└────────────────────────────────────────────────────────────────┘
┌─ Último TX ─┐┌────────── Log ──────────────────┐┌─ Último RX ─┐
│ Dst: ff:... ││ [RX] Capturado...               ││ Dst: 33:... │
│ Src: 02:... ││ [TX] Enviado...                 ││ Src: aa:... │
│ Tipo: 0x88B5││ [INFO] Custom cargado           ││ Tipo: 0x86D │
│ Payload:    ││ [WARN] No respuesta             ││ Payload:    │
│ 0000: 42 00││ ...                             ││ 0000: 60 00 │
│ 0010: 00 00││                                 ││ 0010: 00 1E │
│ ...         ││                                 ││ ...         │
└─────────────┘└─────────────────────────────────┘└─────────────┘
```

### Panel de Desglose de Trama (Nuevo)

**Aparece debajo del header si la terminal tiene >30 líneas de altura.**

Muestra la estructura completa del último paquete TX o RX de forma **compacta en 5 líneas**, manteniendo los colores por sección:

*   **Dst (MAC Destino)**: Amarillo + interpretación legible
*   **Src (MAC Origen)**: Cyan + interpretación legible
*   **Type (EtherType)**: Magenta + nombre del protocolo
*   **Payload**: Verde - primeros 16 bytes (truncado si hay más)
*   **Total**: Resumen del tamaño

**Ventajas**:
- Ocupa solo 5 líneas en lugar de 18
- Deja mucho más espacio para el log
- Sigue siendo informativo y coloreado
- Se actualiza automáticamente

### Paneles Laterales TX y RX (Nuevo)

#### Panel de Último TX Enviado (izquierda)
Aparece si la terminal tiene >150 columnas. Muestra:
*   Direcciones MAC origen y destino
*   Tipo de protocolo (EtherType) con nombre
*   **Hex dump completo** del payload con offsets
*   **Vista ASCII** de caracteres imprimibles

Se actualiza automáticamente cuando:
*   Presionas `[s]` o `[d]` (demos)
*   Presionas `[c]` (enviar custom)

#### Panel de Último RX Capturado (derecha)
Aparece si la terminal tiene >100 columnas. Muestra la misma información que el panel TX, pero para paquetes capturados del kernel.

Se actualiza automáticamente cuando:
*   El kernel genera tráfico (ping, curl, IPv6 automático)
*   Presionas `[t]` (demo RX simulado)

### Colores por segmento
El log aplica colores por partes, no por línea completa:

*   **`[RX]`** en verde, **`[TX]`** en rojo, **`[INFO]`** en cian, **`[WARN]`** en amarillo.
*   El campo **`proto=...`** se resalta en cian para distinguir el protocolo.

### Scroll del log
El panel de log tiene un **scrollbar** vertical en el borde derecho:

*   Flechas ↑/↓ desplazan línea a línea.
*   PgUp/PgDn desplazan por bloques.
*   El diamante indica la posición actual dentro del historial.

### Edición y Captura de Custom

*   **`e` (Editar)**: Abre el archivo `custom_packet.hex` en `$EDITOR` (nano/vi). El programa suspende ncurses, espera a que **cierres el editor**, reinicia ncurses y recarga automáticamente el archivo. Esto evita crashes de terminal.
*   **`x` (Capturar RX)**: Guarda el último paquete RX capturado como custom en hex format legible para humanos.

### Scroll y Navegación

*   El scroll es intuitivo: **Flechas arriba = sube en el log**, **flechas abajo = baja en el log**.
*   La **scrollbar** (diamante vertical derecho) se posiciona según la altura actual en el historial.
*   **PgUp/PgDn** saltan por bloques de 5 líneas.

### Demos (Paquetes de Prueba)

*   **`s` (Demo TX 0x00)**: Envía un paquete de demostración con payload relleno de 0x00. Se muestra como `[TX]` en el log y actualiza el panel TX.
*   **`d` (Demo TX 0xFF)**: Envía un paquete de demostración con payload relleno de 0xFF. Se muestra como `[TX]` en el log y actualiza el panel TX.
*   **`t` (Demo RX simulado)**: Simula que el kernel envía un paquete demo (como si alguien hiciera `ping`). Se muestra como `[RX]` en el log y actualiza el panel RX.
*   **`c` (Enviar custom)**: Envía el paquete custom cargado desde `custom_packet.hex`. Actualiza el panel TX con el contenido enviado.
*   **`b` (Toggle TX/RX)**: Alterna el panel de desglose entre mostrar el último TX enviado o el último RX capturado.
*   Todos usan la estructura de trama Ethernet estándar (14 bytes de cabecera + 46 bytes de payload mínimo).

### Pantalla de información (Paginada)
Con la tecla `i` se abre una pantalla de ayuda con 3 páginas. Navega con:
*   **`/`** (barra): página anterior
*   **`*`** (asterisco): página siguiente

Las 3 páginas cubren:
1.  **Conceptos básicos**: TX/RX, TAP, estructura Ethernet, EtherType, payload.
2.  **Editar paquetes custom**: Formato hex, estructura mínima, ejemplo de edición.
3.  **Valores de bits y ejemplos**: MAC addresses, códigos de EtherType, patrones de payload.

### Panel Lateral de RX

Cuando la terminal tiene más de 100 columnas de ancho, aparece un **panel lateral derecho** que muestra en tiempo real el último paquete RX capturado:

*   **Cabecera Ethernet**: Direcciones MAC origen/destino y tipo de protocolo.
*   **Hex Dump**: Vista hexadecimal del payload con offsets (como `hexdump -C`).
*   **Vista ASCII**: Caracteres imprimibles al lado del hex, otros se muestran como `.`
*   **Actualización automática**: Se actualiza cada vez que llega un nuevo paquete RX.

---

## 5. Pruebas Prácticas desde Terminal

### ¿Cómo funciona la captura RX en la práctica?

Cuando ejecutas comandos de red normales en otra terminal (mientras la app TUI está corriendo), el kernel Linux genera tráfico que intenta salir por la interfaz `tap0`. La app lo captura ANTES de que llegue a ningún destino físico.

### Configuración Inicial Requerida

```bash
# 1. Crear la interfaz TAP
sudo ip tuntap add dev tap0 mode tap

# 2. Activar la interfaz (equivalente a "conectar el cable")
sudo ip link set up dev tap0

# 3. Asignar una dirección IP (necesario para ping/curl)
sudo ip addr add 192.168.100.50/24 dev tap0

# 4. (Opcional) Añadir ruta para redirigir tráfico por tap0
sudo ip route add 192.168.100.0/24 dev tap0
```

### Prueba 1: Ping (Protocolo ICMP)

```bash
# En otra terminal (mientras la app TUI corre):
ping -I tap0 192.168.100.1
```

**Qué verás en la app**:
```
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=ARP   payload=46 bytes
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=IPv4  payload=84 bytes
```

**Explicación**:
1. Primero el kernel envía un **ARP request** buscando quién tiene la IP 192.168.100.1 (broadcast a ff:ff:ff:ff:ff:ff)
2. Como nadie responde, envía el **ICMP Echo Request** (ping) de todas formas

**Qué puedes hacer**:
- Presiona `[x]` para guardar el paquete ARP o ICMP como custom
- Presiona `[c]` para reinyectarlo al kernel (loop de prueba)

### Prueba 2: ARP Probing (Protocolo ARP)

```bash
# Instala arping si no lo tienes
sudo apt install arping

# Lanzar solicitud ARP
sudo arping -I tap0 -c 3 192.168.100.200
```

**Qué verás**:
```
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=ARP   payload=46 bytes
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=ARP   payload=46 bytes
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=ARP   payload=46 bytes
```

**Anatomía del paquete ARP capturado**:
- **Dst MAC**: `ff:ff:ff:ff:ff:ff` (broadcast, pregunta "a todos")
- **Src MAC**: Tu MAC del tap0
- **EtherType**: `0x0806` (ARP)
- **Payload**: 28 bytes de ARP header + 18 bytes de padding = 46 bytes

### Prueba 3: Tráfico IPv6 Automático (Neighbor Discovery)

```bash
# No necesitas hacer nada, solo:
sudo ip link set up dev tap0
```

**Qué verás automáticamente**:
```
[RX] aa:bb:cc:dd:ee:ff -> 33:33:00:00:00:02  proto=IPv6  payload=62 bytes
[RX] aa:bb:cc:dd:ee:ff -> 33:33:ff:xx:yy:zz  proto=IPv6  payload=78 bytes
```

**Explicación**:
- El kernel activa IPv6 por defecto en interfaces nuevas
- Envía mensajes **ICMPv6 Router Solicitation** (busca routers)
- Envía **Neighbor Discovery** (equivalente a ARP para IPv6)
- **Dst MAC** empieza con `33:33:` (multicast IPv6)

**Para desactivar este ruido**:
```bash
sudo sysctl -w net.ipv6.conf.tap0.disable_ipv6=1
```

### Prueba 4: Conexión TCP (Curl/Netcat)

```bash
# Intenta conectar a un servidor web por tap0
curl --interface tap0 http://192.168.100.1
```

**Qué verás**:
```
[RX] aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff  proto=ARP   payload=46 bytes
[RX] aa:bb:cc:dd:ee:ff -> [gateway_mac]      proto=IPv4  payload=74 bytes  # SYN packet
[RX] aa:bb:cc:dd:ee:ff -> [gateway_mac]      proto=IPv4  payload=66 bytes  # ACK packet
```

Verás:
1. **ARP request** para resolver la MAC del gateway
2. **TCP SYN** (intento de abrir conexión HTTP)
3. Posibles **retransmisiones** si nadie responde

### Prueba 5: Inyección TX + Captura RX (Loop)

```bash
# En la app TUI:
# 1. Presiona [s] para enviar demo TX
# 2. Mira el log: aparece [TX] en rojo
# 3. El kernel recibe el paquete y lo procesa
# 4. Si el kernel responde (ej: ICMP unreachable), verás [RX] en verde
```

**Flujo completo**:
```
[TX] ff:ff:ff:ff:ff:ff -> 02:00:00:00:00:01  proto=Demo  payload=46 bytes  (tu app envía)
[RX] (posible respuesta del kernel si procesa el paquete)
```

### Anatomía Completa de una Trama Ethernet Capturada

Cuando capturas un paquete con `[x]` y lo abres con `[e]`, ves esto:

```hex
# custom_packet.hex (ejemplo real de ARP request)

# Cabecera Ethernet (14 bytes obligatorios)
ff ff ff ff ff ff    # [0-5]   Dst MAC: broadcast
aa bb cc dd ee ff    # [6-11]  Src MAC: tu interfaz
08 06                # [12-13] EtherType: 0x0806 (ARP)

# Payload ARP (28 bytes reales)
00 01                # Hardware type: Ethernet (1)
08 00                # Protocol type: IPv4 (0x0800)
06                   # HW address length: 6 (MAC)
04                   # Protocol address length: 4 (IPv4)
00 01                # Opcode: 1 (request)
aa bb cc dd ee ff    # Sender MAC
c0 a8 64 32          # Sender IP: 192.168.100.50
00 00 00 00 00 00    # Target MAC: desconocida (00:00:00:00:00:00)
c0 a8 64 01          # Target IP: 192.168.100.1

# Padding (18 bytes de relleno para llegar a 46)
00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
```

**Total**: 14 (cabecera) + 28 (ARP) + 18 (padding) = **60 bytes** (mínimo Ethernet)

### ¿Por Qué el Padding Automático?

El código en `serializeEthernetII()` hace:

```cpp
if (frame.payload.size() < 46) {
    buffer.resize(14 + 46);  // Rellena con ceros hasta 46 bytes de payload
}
```

**Razón**: El estándar IEEE 802.3 exige un **tamaño mínimo de trama de 64 bytes** (incluyendo 4 bytes de FCS/CRC que el hardware añade). Como tu programa no maneja el CRC (lo hace el driver), debe enviar mínimo 60 bytes (14 cabecera + 46 payload).

### Herramientas Adicionales para Testing

```bash
# 1. Tcpdump (para comparar con tu app)
sudo tcpdump -i tap0 -e -xx

# 2. Hping3 (enviar paquetes custom desde fuera)
sudo hping3 -I tap0 --rawip --data 100 192.168.100.1

# 3. Scapy (Python interactivo)
sudo scapy
>>> sendp(Ether(dst="ff:ff:ff:ff:ff:ff")/IP()/ICMP(), iface="tap0")
```

### Casos de Uso Avanzados

1. **Fuzzing de protocolos**: Modifica el payload hex para enviar tramas malformadas y ver cómo reacciona el kernel.
2. **ARP Spoofing simulado**: Captura un ARP reply, edita la MAC origen, reenvía con `[c]`.
3. **Monitor de broadcast**: Deja la app corriendo y observa qué servicios del sistema usan broadcast (DHCP, NetBIOS, etc).
4. **Testing de firewall**: Configura `iptables` en tap0 y verifica qué paquetes bloquea observando el log RX.

### Notas Importantes

- **Sin respuestas**: Como la app captura pero no responde automáticamente, verás muchos "timeouts" en ping/curl. Esto es normal.
- **Broadcast inicial**: Al activar tap0, verás 2-3 paquetes IPv6 automáticos. Es el kernel anunciándose en la red.
- **Performance**: En modo no bloqueante con poll, la app consulta el TAP cada 100ms. Si necesitas capturar tráfico de alta velocidad, ajusta el timeout en `poll()`.

---

## 6. Estructura Interna de Ethernet II (Sin "Magic Headers")

### Mito: "Ethernet tiene un campo de longitud antes del payload"

**Falso para Ethernet II** (el que usamos). Confusión común con IEEE 802.3 (Ethernet original).

### Realidad: Solo 14 bytes de cabecera fija

```
┌─────────────────────────────────────────────┐
│  Dst MAC (6) │ Src MAC (6) │ EtherType (2)  │  ← Cabecera (14 bytes)
├─────────────────────────────────────────────┤
│          Payload (46-1500 bytes)            │  ← Datos útiles
│              (sin longitud explícita)       │
└─────────────────────────────────────────────┘
```

**¿Cómo sabe el receptor cuántos bytes tiene el payload?**

1. **El hardware de red** cuenta los bytes físicos recibidos antes de EOF (End of Frame)
2. **El driver del kernel** pasa el tamaño total a `tap.read()`
3. **Tu código** resta 14: `payloadSize = bytesLeidos - 14`

### Comparación: Ethernet II vs IEEE 802.3
 _____________________________________________________________________
| Aspecto          | Ethernet II (usado aquí) | IEEE 802.3 (antiguo) |
|------------------|--------------------------|----------------------|
| Campo byte 12-13 | EtherType (tipo)         | Length (longitud)    |
| Identificador    | ≥ 0x0600                 | ≤ 1500               |
| Uso moderno      | **Estándar actual**      | Raro (Token Ring)    |

**Discriminador**: Si el campo vale ≥ 1536 (0x0600), es EtherType. Si vale ≤ 1500, es longitud.

### ¿Por qué funciona editar solo el payload?

```cpp
// parseHexBytesFile() convierte TODO el archivo hex a bytes
std::vector<uint8_t> rawBytes = {0xff, 0xff, ..., 0x00, 0x00}; // N bytes

// parseEthernetII() divide:
EthernetFrame frame;
frame.dst   = rawBytes[0..5];     // Primeros 6
frame.src   = rawBytes[6..11];    // Segundos 6
frame.type  = rawBytes[12..13];   // Siguientes 2
frame.payload.assign(rawBytes.begin() + 14, rawBytes.end()); // TODO el resto
```

**No hay validación de longitud** porque:
- El estándar confía en que el tamaño físico es correcto
- El padding lo añade `serializeEthernetII()` automáticamente si falta
- Ethernet asume que si llegó al receptor, el tamaño era válido (CRC correcto)

### Ejemplo práctico: Añadir 10 bytes al payload

```hex
# custom_packet.hex ANTES (60 bytes totales)
ff ff ff ff ff ff aa bb cc dd ee ff 88 b5
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00

# custom_packet.hex DESPUÉS (70 bytes totales)
ff ff ff ff ff ff aa bb cc dd ee ff 88 b5
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00
AA BB CC DD EE FF 11 22 33 44   # 10 bytes nuevos
```

**Resultado automático**:
- `frame.payload.size()` pasa de 46 a 56
- `describeEthernetII()` muestra "payload=56 bytes"
- **No necesitas tocar ningún "campo de longitud"** porque no existe

### ¿Y el CRC/FCS (Frame Check Sequence)?

**No lo manejas tú**. Los últimos 4 bytes de una trama Ethernet física son el CRC:
- Lo **añade automáticamente** el hardware de red al transmitir
- Lo **verifica y elimina** el hardware al recibir
- Tu programa **nunca ve esos 4 bytes** - el driver ya los procesó

Por eso el código trabaja con tramas de "60 bytes mínimo" en lugar de 64: los 4 del CRC son invisibles para la aplicación.
