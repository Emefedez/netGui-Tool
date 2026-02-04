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

La aplicación usa una interfaz en terminal con paneles fijos para estado, log y ayuda.

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

*   **`s` (Demo TX 0x00)**: Envía un paquete de demostración con payload relleno de 0x00. Se muestra como `[TX]` en el log.
*   **`d` (Demo TX 0xFF)**: Envía un paquete de demostración con payload relleno de 0xFF. Se muestra como `[TX]` en el log.
*   **`t` (Demo RX simulado)**: Simula que el kernel envía un paquete demo (como si alguien hiciera `ping`). Se muestra como `[RX]` en el log.
*   Todos usan la estructura de trama Ethernet estándar (14 bytes de cabecera + 46 bytes de payload mínimo).

### Pantalla de información (Paginada)
Con la tecla `i` se abre una pantalla de ayuda con 3 páginas. Navega con:
*   **`/`** (barra): página anterior
*   **`*`** (asterisco): página siguiente

Las 3 páginas cubren:
1.  **Conceptos básicos**: TX/RX, TAP, estructura Ethernet, EtherType, payload.
2.  **Editar paquetes custom**: Formato hex, estructura mínima, ejemplo de edición.
3.  **Valores de bits y ejemplos**: MAC addresses, códigos de EtherType, patrones de payload.
