# Configuració de l'ESP32

- `build/sdkconfig`: configura els valors locals del firmware, incloent-hi WiFi, destinació UDP i opcions d'ESP-IDF; està exclòs de Git.
- `sdkconfig.defaults`: configura els valors inicials compartits de la destinació UDP, el port i l'interval; es versiona a Git sense credencials WiFi.
- `Kconfig.projbuild`: defineix les opcions de WiFi i UDP disponibles a `menuconfig`, amb els seus valors per defecte i límits.
- `build/sdkconfig.old` (si es genera): conserva la còpia anterior del `sdkconfig` creada per ESP-IDF; està exclòs de Git.

Des del directori `src`:

```sh
cmake --preset target-analyzer
idf.py -B build/target-analyzer menuconfig
cmake --build --preset target-analyzer
```

La configuració local es conserva encara que s'esborri `src/build`.
La carpeta `src/config/build/` està exclosa de Git; si s'esborra, cal tornar
a configurar les credencials WiFi. Els fitxers derivats de compilació, com
`sdkconfig.h` i `sdkconfig.cmake`, es generen a `src/build/target-analyzer/config/`.

En un clon nou, `sdkconfig.defaults` estableix la destinació UDP
`192.168.42.34:3333` i l'interval de `1000` ms. Només cal introduir l'SSID i
la contrasenya a **Analyzer WiFi and UDP** de `menuconfig`, sempre que el
receptor mantingui aquesta IP. Els valors locals ja desats a `build/sdkconfig`
tenen prioritat sobre els de `sdkconfig.defaults`.
