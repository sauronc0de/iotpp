# Hello World per UDP

L'aplicació utilitza dos components locals a `src/lib`:

- `wifi`: inicialització de l'estació, reconnexió automàtica i espera d'una IP.
- `udp`: client IPv4 amb gestió del socket i enviament de datagrames.

`main.cpp` passa les credencials i el destí als components i coordina els
enviaments periòdics. Els components no depenen de la configuració d'Analyzer
ni l'un de l'altre. Internament utilitzen els components d'ESP-IDF `esp_wifi`,
`esp_netif`, `esp_event`, `nvs_flash` i `lwip`; no cal instal·lar biblioteques externes.

## Configuració i compilació

Des del directori `src`, amb l'entorn ESP-IDF carregat:

```sh
cmake --preset target-analyzer
idf.py -B build/target-analyzer menuconfig
```

Al menú **Analyzer WiFi and UDP**, configura:

- **WiFi SSID** i **WiFi password**: les credencials d'una xarxa de 2,4 GHz.
  Deixa la contrasenya buida si la xarxa és oberta.
- **Destination IPv4 address**: l'adreça IPv4 de l'ordinador receptor
  (`192.168.42.34` en un clon nou, definida a `sdkconfig.defaults`).
- **Destination UDP port**: el port del receptor (per defecte, `3333`).
- **Message interval**: l'interval entre missatges (per defecte, `1000` ms).

La configuració es desa a `src/config/build/sdkconfig`, exclòs de Git. Es conserva
encara que s'esborri el directori `src/build`. Consulta
[`src/config/README.md`](../../config/README.md) per veure la funció de cada
fitxer de configuració de l'ESP32.

Compila, grava i obre el monitor sèrie (substitueix el port pel de la placa):

```sh
cmake --build --preset target-analyzer
idf.py -B build/target-analyzer -p /dev/ttyUSB0 flash monitor
```

## Recepció dels missatges

A l'ordinador receptor, connectat a una xarxa accessible des de l'ESP32:

```sh
nc -u -l 3333
```

Permet el trànsit UDP entrant al port seleccionat si hi ha un tallafoc.
Rebràs un `Hello World from iotpp analyzer!` cada segon. El monitor sèrie
mostra la IP assignada i els enviaments o errors. L'aplicació espera a tenir
una IP abans d'enviar i intenta reconnectar-se si perd la connexió WiFi.
UDP no garanteix el lliurament: el registre d'enviament no confirma la recepció.
