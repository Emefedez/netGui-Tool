#pragma once
#include <string>

/**
 * @brief Thin wrapper around a Linux TAP device (Ethernet L2 frames).
 *
 * This class opens and configures a TAP interface via `/dev/net/tun`.
 *
 * Notes:
 * - Requires Linux.
 * - Typically requires `CAP_NET_ADMIN` (run as root or with proper permissions).
 * - When created with IFF_NO_PI, reads/writes are raw Ethernet frames without
 *   the 4-byte packet information header.
 */

class TapDevice {
private:
    int fd;                 // File descriptor del dispositivo
    std::string dev_name;   // Nombre, ej: "tap0"

public:
    /**
     * @brief Enable/disable non-blocking mode on the TAP file descriptor.
     *
     * When enabled, `read()` returns immediately and sets `errno` to
     * `EAGAIN`/`EWOULDBLOCK` if no packet is available.
     */
    void setNonBlocking(bool non_blocking);

    /**
     * @brief Create (or attach to) a TAP device with the requested name.
     * @param name Requested interface name (e.g. "tap0").
     *
     * The kernel may adjust the name (e.g. if the requested one is busy).
     * The real interface name can be retrieved via `name()`.
     *
     * @throws std::runtime_error on failure.
     */
    TapDevice(const std::string& name);

    /** @brief Close the TAP file descriptor (if open). */
    ~TapDevice();
    
    /**
     * @brief Read one Ethernet frame from the TAP device.
     * @return Number of bytes read, 0 on EOF, or -1 on error (check `errno`).
     */
    int read(unsigned char* buffer, size_t size);
    
    /**
     * @brief Write an Ethernet frame to the TAP device.
     * @return Number of bytes written, or -1 on error (check `errno`).
     */
    int write(unsigned char* buffer, size_t size);

    /**
     * @brief Const overload of write().
     */
    int write(const unsigned char* buffer, size_t size);

    /** @brief Returns the kernel-assigned interface name (e.g. "tap0"). */
    const std::string& name() const { return dev_name; }
};
