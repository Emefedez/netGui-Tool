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
- `t`: recibir demo simulado (como si viniera del kernel RX)
- `c`: enviar paquete custom
- `e`: editar paquete custom (abre en $EDITOR, espera y recarga automáticamente, sin crashes)
- `r`: recargar paquete custom desde archivo
- `x`: guardar el último paquete RX capturado como custom
- `i`: ayuda/explicación (3 páginas con [-] y [+] para navegar)
- Flechas UP/DOWN: scroll línea a línea (intuitivo)
- PgUp/PgDn: scroll por bloques de 5 líneas
- `q`: salir

Notas:
- El log tiene colores por tipo (RX/TX/INFO/WARN) y por segmento (ej. protocolo en cian).
- La barra lateral derecha muestra scroll con diamante posicionado según altura actual.
- Al editar custom con `e`, ncurses se suspende, el editor toma control total, y se reinicia sin crashes.
- Con `x` puedes capturar paquetes RX reales y usarlos como plantilla custom.
- Los demos `s` y `d` muestran el tamaño exacto en bytes enviados.


