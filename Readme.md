###REIMPLEMENTACIÓN DE DIVERSOS PROTOCOLOS DE RED; EMPEZANDO POR ETHERNET Y TAP###

Usamos una interfaz TUI (ncurses) para hacer una herramienta interactiva y facil de usar en terminal.

El código del ethernet usará un TAP, representación
virtual del Ethernet que recibe bits y sabe que tipo
de información contienen según la cabecera.

Ejecuta esto en linux para establecer parámetros:

```bash
sudo ip tuntap add dev tap0 mode tap
sudo ip link set dev tap0 up
sudo ip addr add 10.0.0.1/24 dev tap0
```

Controles de la TUI:

- `s`: enviar demo con payload 0x00
- `d`: enviar demo con payload 0xFF
- `c`: enviar paquete custom
- `e`: editar paquete custom (archivo `custom_packet.hex`)
- `r`: recargar paquete custom
- `i`: ayuda/explicación
- Flechas / PgUp / PgDn: scroll del log
- `q`: salir

Notas:
- El log tiene colores por tipo (RX/TX/INFO/WARN).
- La barra lateral muestra la posición de scroll del log.

https://github.com/user-attachments/assets/7b9cfc4c-973b-4271-873e-2cb22caf2c0e

