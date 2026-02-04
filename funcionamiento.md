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
Dado que TAP es una interfaz virtual, los conceptos de "Lectura" y "Escritura" son inversos respecto al punto de vista del Kernel:

1.  **Escritura (`tap.write`) = Inyección "Rx" al Kernel**:
    *   Cuando el programa llama a `write`, el Kernel de Linux **recibe** esos datos como si hubieran llegado por un cable físico.
    *   *Uso*: Simular tráfico entrante para ver cómo responde el sistema operativo (ej. enviar un paquete ARP Reply falso).
2.  **Lectura (`tap.read`) = Captura "Tx" del Kernel**:
    *   Cuando el programa llama a `read`, obtiene los datos que el Kernel está intentando **enviar** hacia la red.
    *   *Uso*: Sniffing o captura de tráfico generado por aplicaciones o por el propio sistema (ej. capturar un `ping` que el usuario ejecuta en la terminal).

### Configuración del SO (Prerrequisitos)
El código asume que la interfaz virtual ya ha sido configurada externamente. Sin estos pasos, `tap.read` no recibirá nada y `tap.write` enviará paquetes a la nada:

*   **Creación**: `ip tuntap add dev tap0 mode tap` (Crea la tubería).
*   **Enlace (Link Up)**: `ip link set up dev tap0` (Equivalente a conectar el cable).
*   **Direccionamiento (Opcional)**: `ip addr add 192.168.X.X/24 dev tap0` (Necesario para que el Kernel responda a protocolos IP/ICMP).
