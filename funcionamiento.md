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


### Cómo `ethernet.cpp` representa el protocolo

`ethernet.cpp` junto con `ethernet.h` modelan Ethernet II aplicando una correspondencia directa entre los campos de la especificación y las estructuras/funciones del código:

- `EthernetFrame` : estructura que contiene `dst` (6 bytes), `src` (6 bytes), `etherType` (2 bytes) y `payload` (vector de bytes). Representa una trama Ethernet completa tal como la ve la aplicación.
- `macToString()` / `parseMac()` : utilidades para convertir MACs binarias <-> representación hexadecimal legible, usadas en logs y en la UI.
- `toHex()` : genera un volcado hexadecimal legible del `payload` o de cualquier buffer para mostrar en los paneles y en `custom_packet.hex`.
- `parseEthernetII(const uint8_t* data, size_t size)` : toma un buffer crudo recibido (p. ej. desde `tap.read()`), valida que haya al menos 14 bytes y separa los primeros 14 bytes en `dst`, `src` y `etherType`, devolviendo el `payload` restante en el `EthernetFrame`.
- `serializeEthernetII(const EthernetFrame&)` : empaqueta una instancia de `EthernetFrame` a una secuencia plana de bytes lista para enviar por el TAP. Concatena la cabecera (6+6+2) y el `payload`, y aplica *padding* hasta 46 bytes de `payload` cuando es necesario. No añade FCS/CRC — eso lo hace el driver/hardware.
- `describeEthernetII(const EthernetFrame&)` : construye una cadena resumen ("origen -> destino, tipo, tamaño") pensada para logs y la TUI.
- `parseHexBytesFile()` : convierte el contenido textual de `custom_packet.hex` en bytes binarios para cargar paquetes custom desde el editor.

Flujo típico en la aplicación:

1. Llegan bytes desde el kernel vía TAP (`tap.read()`).
2. `parseEthernetII()` transforma esos bytes en un `EthernetFrame` (si el tamaño es válido).
3. La TUI usa `describeEthernetII()` y `toHex()` para renderizar la línea del log y los paneles (hex + ASCII + campos).
4. Si el usuario edita o carga un paquete custom, `parseHexBytesFile()` produce un `EthernetFrame` en memoria.
5. Para enviar, la app llama a `serializeEthernetII()` y escribe los bytes resultantes con `tap.write()`; el kernel/driver añade FCS y procesa físicamente la trama.

Ejemplo mínimo de uso:

```cpp
auto opt = parseEthernetII(buf, n);
if (opt) {
    EthernetFrame frame = *opt;
    std::string summary = describeEthernetII(frame);
    std::vector<uint8_t> bytes = serializeEthernetII(frame);
    tap.write(bytes.data(), bytes.size());
}
```

Notas importantes:

- El padding a 46 bytes de `payload` asegura el tamaño mínimo de trama (60 bytes visibles por la aplicación). El FCS/CRC de 4 bytes queda fuera del espacio de usuario.
- `parseEthernetII()` no intenta reconstruir FCS ni validar CRC; confía en que el driver entregó una trama completa.
- Estas funciones permiten separar con claridad la representación en memoria (estructuras C++) de la representación en el medio físico (buffer de bytes), lo que facilita su uso por la TUI, por las rutinas de logging y por las acciones de inyección (`[c]`, `[s]`, `[d]`).

### Comentarios restaurados (aclaraciones útiles)

- Los campos `hardwareType`, `protocolType` y `opcode` en ARP están en **big-endian**, por eso se convierten con `ntohs()`.
- En ARP, los tamaños (`hardwareSize`, `protocolSize`) permiten soportar otros medios, pero aquí se **restringen** a Ethernet (6) e IPv4 (4).
- Una ARP Request siempre lleva **Target MAC = 00:00:00:00:00:00** porque se está preguntando por ella.
- Para ARP Reply, se invierten roles: el solicitante pasa a ser `target` y el respondedor pasa a ser `sender`.
- El padding de Ethernet aplica igual para ARP: si el payload es menor a 46 bytes, se rellena a ese mínimo.

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
┌───── Desglose Trama TX/RX (compacto) ──────────────────────────┐
│ Dst: FF FF FF FF FF FF | ff:ff:ff:ff:ff:ff                     │
│ Src: 02 00 00 00 00 01 | 02:00:00:00:00:01                     │
│ Type: 88 B5 | 0x88B5 (Demo)                                    │
│ Payload: 42 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...   │
│ Total: 60 bytes (14 header + 46 payload)                       │
└────────────────────────────────────────────────────────────────┘
┌─ Último TX ─┐┌────────── Log ──────────────────┐┌─ Último RX ─┐
│ Dst: ff:... ││ [RX] Capturado...               ││ Dst: 33:... │
│ Src: 02:... ││ [TX] Enviado...                 ││ Src: aa:... │
│ Tipo: 0x88B5││ [INFO] Custom cargado           ││ Tipo: 0x86D │
│ Payload:    ││ [WARN] No respuesta             ││ Payload:    │
│ 0000: 42 00 ││ ...                             ││ 0000: 60 00 │
│ 0010: 00 00 ││                                 ││ 0010: 00 1E │
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
# Documentación técnica (refactorizada)

Resumen: guía compacta y esquemática del diseño, API y flujo de datos de la herramienta. Mantiene todos los detalles técnicos importantes (TAP, Ethernet II, TUI, ejemplos y pruebas).

## A. TAP (include/tap.h, src/tap.cpp)

Propósito: crear y usar una interfaz virtual TAP (Ethernet L2) para leer/escribir tramas desde/hacia el kernel.

Estado y API clave
- `int fd`: descriptor abierto sobre `/dev/net/tun`.
- `std::string dev_name()`: nombre real de la interfaz (p. ej. `tap0`).
- `TapDevice(const std::string& name)`: constructor que configura `IFF_TAP | IFF_NO_PI` vía `ioctl`.
- `~TapDevice()`: cierra `fd`.
- `setNonBlocking(bool)`: activa `O_NONBLOCK` si procede.
- `int read(unsigned char* buf, size_t n)`: devuelve bytes leídos o -1.
- `int write(unsigned char* buf, size_t n)`: escribe bytes al TAP.

Notas operativas
- El programa asume que la interfaz existe y está `up` (ver sección de configuración).
- Operaciones no bloqueantes retornan inmediatamente cuando no hay datos.

## B. Ethernet (include/ethernet.h, src/ethernet.cpp)

Modelado y tipos
- `using MacAddress = std::array<uint8_t,6>`
- `struct EthernetFrame { MacAddress dst, src; uint16_t etherType; std::vector<uint8_t> payload; }`
- `namespace EtherType` contiene constantes (`IPv4`, `ARP`, `IPv6`, `Demo`).

Funciones principales (qué hacen y cuándo usarlas)
- `parseEthernetII(const uint8_t* data, size_t size) -> optional<EthernetFrame>`: valida >=14 bytes, separa dst/src/etherType y devuelve payload.
- `serializeEthernetII(const EthernetFrame&) -> vector<uint8_t>`: concatena 6+6+2 + payload y aplica padding hasta 46 bytes de payload cuando sea necesario (no añade FCS/CRC).
- `describeEthernetII(const EthernetFrame&) -> string`: resumen legible para logs/UI.
- `macToString()` / `parseMac()` : conversión MAC ↔ texto.
- `toHex()` : volcado hex legible.
- `parseHexBytesFile(string) -> optional<vector<uint8_t>>`: parsea `custom_packet.hex` (ignora comentarios) a bytes.

Diseño: separación clara entre representación en memoria (`EthernetFrame`) y wire-format (buffer de bytes). Esto facilita la TUI, logging e inyección (`tap.write`).

Ejemplo de uso mínimo
```cpp
auto opt = parseEthernetII(buf, n);
if (opt) {
  EthernetFrame f = *opt;
  auto summary = describeEthernetII(f);
  auto bytes = serializeEthernetII(f);
  tap.write(bytes.data(), bytes.size());
}
```

Notas importantes
- Padding: `serializeEthernetII()` asegura al menos 46 bytes de payload (14+46 = 60 bytes visibles). El FCS/CRC (4 bytes) lo añade el driver.
- `parseEthernetII()` no verifica CRC ni reconstruye FCS; confía en que el driver entregó una trama completa.

## C. Flujo de datos y terminología (TX/RX)

Perspectiva del programa (clave para evitar confusión):
- TX (programa) = `tap.write()` → Programa escribe, kernel recibe (desde la perspectiva del kernel es RX).
- RX (programa) = `tap.read()` → Programa lee, kernel envía (desde la perspectiva del kernel es TX).

Tabla rápida
| API | Programa | Kernel |
|---:|:--------:|:------:|
| `write()` | TX (envía) | recibe |
| `read()`  | RX (recibe) | envía |

Implicación práctica: guardar con `[x]` captura lo que el kernel generó; reenviar con `[c]` inyecta de nuevo tráfico "entrante" al kernel.

## D. Interfaz TUI (ncurses) — estructura resumida

Layout adaptativo
- Header compacto (estado, interfaz).
- Panel log central con wrapping y scrollbar.
- Paneles laterales condicionales:
  - Último TX (izquierda, ancho >=150 cols): MACs, EtherType, hex dump y ASCII.
  - Último RX (derecha, ancho >=100 cols): igual que TX pero para capturas.
- Panel compacto de desglose (mostrar 5 líneas) cuando la altura >=30.

Colores y semántica
- `[RX]` verde, `[TX]` rojo, `[INFO]` cian, `[WARN]` amarillo.
- Proto/fields resaltados (ej. `proto=` en cian).

Controles principales
- `s` demo TX 0x00; `d` demo TX 0xFF; `t` demo RX simulado; `c` enviar custom; `x` guardar último RX como custom.
- `e` editar `custom_packet.hex` (suspende ncurses, reanuda y recarga).
- Navegación: flechas, PgUp/PgDn y scrollbar.

Comportamiento de inyección
- El flujo de inyección es: parsear/leer/editar -> `serializeEthernetII()` -> `tap.write()` -> driver añade FCS y procesa la trama.

## E. Pruebas y comandos útiles

Creación/activar TAP
```bash
sudo ip tuntap add dev tap0 mode tap
sudo ip link set up dev tap0
sudo ip addr add 192.168.100.50/24 dev tap0   # opcional
sudo ip route add 192.168.100.0/24 dev tap0   # opcional
```

Ejemplos de test
- Ping (desde otra terminal): `ping -I tap0 192.168.100.1` — verás ARP y luego ICMP.
- ARP probing: `sudo arping -I tap0 -c 3 192.168.100.200`.
- Curl por interfaz: `curl --interface tap0 http://192.168.100.1`.

Herramientas de comparación
- `sudo tcpdump -i tap0 -e -xx`
- `sudo hping3 -I tap0 --rawip --data 100 192.168.100.1`
- `scapy` sendp(Ether(...), iface="tap0")

Ejemplo de trama en `custom_packet.hex` (ARP request)
```hex
ff ff ff ff ff ff  aa bb cc dd ee ff  08 06
00 01 08 00 06 04 00 01 aa bb cc dd ee ff c0 a8 64 32 00 00 00 00 00 00 c0 a8 64 01
...padding hasta 46 bytes de payload...
```

## F. Casos de uso avanzados y notas

- Fuzzing: editar payloads malformados y observar reacciones del stack.
- ARP spoofing simulado: capturar (`x`), editar (`e`), reinyectar (`c`).
- Monitor broadcast: útil para DHCP/ND discovery.
- Firewall testing: combinar con `iptables` sobre `tap0`.

Rendimiento
- En modo no bloqueante la app muestrea el TAP periódicamente (p. ej. cada 100 ms). Ajustar si necesitas altas tasas.

## G. Ethernet II — estructura y aclaraciones técnicas

- Cabecera fija: 6 dst | 6 src | 2 etherType (14 bytes total).
- Payload: 46–1500 bytes (sin campo explícito de longitud en Ethernet II).
- El driver/hardware gestiona CRC/FCS (4 bytes) — la app nunca lo ve.

Discriminador EtherType vs Length
- Si bytes 12-13 >= 0x0600 → EtherType (tipo de protocolo).
- Si bytes 12-13 <= 1500 → campo length (modelo IEEE 802.3).

Por qué funciona editar solo el payload
- `parseHexBytesFile()` mapea directamente bytes[0..13] → cabecera y el resto a `payload`.
- `serializeEthernetII()` añade padding si `payload.size() < 46`.

---

Si quieres, aplico estos cambios también al `Readme.md` o genero una versión en inglés.

