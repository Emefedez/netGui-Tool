#include "tap.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdexcept>
#include <iostream>

/**
 * @brief Create/configure a TAP interface using the Linux TUN/TAP driver.
 *
 * This configures the device in TAP mode (L2 Ethernet frames) and disables the
 * additional 4-byte packet information header (IFF_NO_PI).
 */
TapDevice::TapDevice(const std::string& name) : dev_name(name) {
    // Abrir el dispositivo clonador TUN/TAP
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("Error opening /dev/net/tun");
        throw std::runtime_error("Failed to open TUN device");
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    // IFF_TAP: Paquetes Ethernet completos
    // IFF_NO_PI: No Packet Information (sin cabecera extra)
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI; 
    std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);

    // Registrar el dispositivo con el kernel
    if (ioctl(fd, TUNSETIFF, (void*)&ifr) < 0) {
        perror("Error setting TUNSETIFF");
        close(fd);
        throw std::runtime_error("Failed to configure TAP device");
    }
        
    // Guardar el nombre real
    dev_name = ifr.ifr_name;
    std::cout << "Dispositivo TAP creado: " << dev_name << " (fd: " << fd << ")\n";
}

/**
 * @brief Close the device file descriptor.
 */
TapDevice::~TapDevice() {
    if (fd >= 0) {
        close(fd);
    }
}

/**
 * @brief Read from the TAP device.
 *
 * In non-blocking mode, returns -1 with errno=EAGAIN/EWOULDBLOCK if no packet
 * is available.
 */
int TapDevice::read(unsigned char* buffer, size_t size) {
    return ::read(fd, buffer, size);
}

/**
 * @brief Write to the TAP device.
 */
int TapDevice::write(unsigned char* buffer, size_t size) {
    return ::write(fd, buffer, size);
}

int TapDevice::write(const unsigned char* buffer, size_t size) {
    return ::write(fd, buffer, size);
}

/**
 * @brief Enable/disable O_NONBLOCK on the TAP file descriptor.
 */
void TapDevice::setNonBlocking(bool non_blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("TapDevice::setNonBlocking (GETFL)");
        return;
    }

    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("TapDevice::setNonBlocking (SETFL)");
    }
}