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
**Video Demo:**

https://github.com/user-attachments/assets/4d9e3035-72be-4305-861d-157fe1b0d9d5


**Info Screen:**
<img width="1024" height="7020" alt="imagen" src="https://github.com/user-attachments/assets/18037c2f-bfb6-438e-87f7-b7d9058aa02e" />


Controles de la TUI:

- `s`: enviar demo con payload 0x00
- `d`: enviar demo con payload 0xFF
- `t`: recibir demo simulado (como si viniera del kernel RX)
- `c`: enviar paquete custom
- `e`: editar paquete custom (abre en $EDITOR, espera y recarga automáticamente, sin crashes)
- `r`: recargar paquete custom desde archivo
- `x`: guardar el último paquete RX capturado como custom
- `i`: ayuda/explicación (3 páginas con [-] y [+] para navegar)
- flechas `UP/DOWN`: scroll línea a línea (intuitivo)
- `PgUp/PgDn`: scroll por bloques de 5 líneas
- `q`: salir

Notas:
- El log tiene colores por tipo (RX/TX/INFO/WARN) y por segmento (ej. protocolo en cian).
- La barra lateral derecha muestra scroll con diamante posicionado según altura actual.
- Al editar custom con `e`, ncurses se suspende, el editor toma control total, y se reinicia sin crashes.
- Con `x` puedes capturar paquetes RX reales y usarlos como plantilla custom.
- Los demos `s` y `d` muestran el tamaño exacto en bytes enviados.

-> **Pendiente**:
  - Arreglar impresion de `/` y `*` erróneas en info.
  - Agregar partes más allá de Ethernet y TAP, limpiar programa.
  - Renombrar todo lo relacionado con `GUI` como `TUI` pues se realizó un cambio para simplificar.
    
