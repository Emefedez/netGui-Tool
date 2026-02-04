###REIMPLEMENTACIÓN DE DIVERSOS PROTOCOLOS DE RED; EMPEZANDO POR ETHERNET Y TAP###

Usamos raylib+raygui para hacer una herramienta interactiva y facil de usar

El código del ethernet usará un TAP, representación
virtual del Ethernet que recibe bits y sabe que tipo
de información contienen según la cabecera.

Ejecuta esto en linux para establecer parámetros:

```bash
sudo ip tuntap add dev tap0 mode tap
sudo ip link set dev tap0 up
sudo ip addr add 10.0.0.1/24 dev tap0
```

https://github.com/user-attachments/assets/7b9cfc4c-973b-4271-873e-2cb22caf2c0e

