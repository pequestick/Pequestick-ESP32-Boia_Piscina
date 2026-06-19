# Pequestick ESP32 Boia Piscina

Firmware per a una boia de piscina basada en **ESP32-C6**, pensada per mesurar la temperatura de l'aigua, integrar-se amb **Home Assistant** via MQTT i poder-se mantenir per Wi-Fi mitjançant OTA.

El projecte ha evolucionat des d'una prova simple amb una sonda **DS18B20** fins a una plataforma IoT força completa amb web pròpia, configuració persistent, MQTT, Home Assistant Discovery, OTA manual, OTA des de GitHub, diagnòstic, configuració de xarxa i preparació per bateria i sensors interns.

---

## Estat actual

Versió actual documentada:

```text
1.6.4-release-helper
```

Funcionalitats principals actuals:

- ESP32-C6 amb web de configuració local.
- Lectura de temperatura d'aigua amb DS18B20.
- Wi-Fi configurable des de la web.
- Mode AP de rescat si no pot connectar a la xarxa.
- MQTT configurable.
- Home Assistant Discovery.
- Controls bàsics des de Home Assistant.
- OTA manual via web.
- OTA des de GitHub mitjançant `firmware/manifest.json`.
- Comprovació d'accés a Internet des de la boia.
- Pantalla OTA millorada amb estat Internet, GitHub, versió remota i actualització disponible.
- Web amb menú lateral, subpàgines i estructura més professional.
- Configuració exportable/importable.
- Preparació per bateria, sensor intern SHT41 i futura gestió energètica.

---

## Hardware actual

### Placa principal

- ESP32-C6 DevKitC-1 durant desenvolupament.
- Preparació futura per migrar a placa ESP32-C6 Mini.

### Sensors

- **DS18B20 impermeable** per temperatura de l'aigua.
- **SHT41** previst per temperatura i humitat internes de la boia.

### Energia

De moment es treballa amb alimentació USB / externa.

Preparació prevista:

- Battery shield amb 2x 18650.
- Bateries 18650 iguals, noves i de marca decent.
- Sense placa solar de moment.

---

## Connexió DS18B20

Connexió actual recomanada:

```text
DS18B20 VCC   -> 3V3
DS18B20 GND   -> GND
DS18B20 DATA  -> GPIO4
Resistència   -> 4.7kΩ entre DATA i 3V3
```

És important que la resistència pull-up sigui estable. Amb cables fluixos o puntes provisionals poden aparèixer lectures fallides, valors `-127 ºC`, `85 ºC` o errors intermitents.

---

## Connexió SHT41 prevista

El SHT41 farà de sensor ambiental intern dins la boia:

```text
SHT41 VCC -> 3V3
SHT41 GND -> GND
SHT41 SDA -> GPIO SDA configurat
SHT41 SCL -> GPIO SCL configurat
```

En una futura ESP32-C6 Mini, la proposta inicial és reservar:

```text
SDA -> GPIO8
SCL -> GPIO9
```

Això encara s'haurà de validar amb el pinout real de la placa final.

---

## Web local

La boia exposa una web local a la IP assignada pel router.

La web inclou:

- Estat general.
- Temperatura.
- Wi-Fi.
- MQTT / Home Assistant.
- Sistema.
- Manteniment.
- Centre d'ajuda.
- JSON d'estat.

La interfície utilitza WebSocket per actualitzar estat en directe quan és possible.

---

## Wi-Fi i AP de rescat

Si la boia no pot connectar a la xarxa configurada, entra en mode AP de rescat.

Dades per defecte:

```text
SSID AP: BOIA-PISCINA-SETUP
IP AP:   http://192.168.4.1
```

Aquest mode permet recuperar la boia després de:

- Canvi de contrasenya Wi-Fi.
- Error configurant IP fixa.
- Reset total.
- Pèrdua d'accés a la xarxa principal.

---

## MQTT

La boia publica dades mitjançant MQTT.

Topics habituals amb `topic_base = boia_piscina`:

```text
boia_piscina/availability
boia_piscina/temperature
boia_piscina/rssi
boia_piscina/uptime
boia_piscina/ip
boia_piscina/total_reads
boia_piscina/valid_reads
boia_piscina/failed_reads
boia_piscina/telemetry
```

També accepta comandes MQTT per accions com reinici i republicació de Discovery.

---

## Home Assistant

El firmware suporta Home Assistant Discovery.

Entitats principals:

```text
sensor.boia_piscina_temperature
sensor.boia_piscina_rssi
sensor.boia_piscina_uptime
sensor.boia_piscina_ip
sensor.boia_piscina_total_reads
sensor.boia_piscina_valid_reads
sensor.boia_piscina_failed_reads
binary_sensor.boia_piscina_online
button.boia_piscina_restart
```

Controls afegits:

```text
number.boia_piscina_read_interval
number.boia_piscina_temperature_decimals
number.boia_piscina_mqtt_publish_interval
switch.boia_piscina_mqtt_enabled
button.boia_piscina_publish_discovery
```

Important: si es desactiva MQTT des de Home Assistant, després cal reactivar-lo des de la web local perquè Home Assistant perd el canal de control.

---

## Històric des de Home Assistant

La web pot mostrar un gràfic de fons amb històric de temperatura llegit des de Home Assistant.

Configuració a la web:

```text
MQTT / HA -> Home Assistant
```

Paràmetres:

- URL de Home Assistant.
- Entity ID de temperatura.
- Token de llarga durada.
- Nombre d'hores a mostrar.

El firmware intenta reduir les dades a mostres horàries per evitar carregar massa memòria a l'ESP32.

---

## OTA manual

La web permet pujar manualment un fitxer `firmware.bin`.

A PlatformIO, després de compilar localment, el binari normalment es troba a:

```text
.pio/build/esp32-c6-devkitc-1/firmware.bin
```

---

## OTA des de GitHub

El firmware pot comprovar un manifest publicat al repo i actualitzar-se directament per Wi-Fi.

Manifest per defecte:

```text
https://raw.githubusercontent.com/pequestick/Pequestick-ESP32-Boia_Piscina/main/firmware/manifest.json
```

El manifest ha de tenir una estructura semblant a:

```json
{
  "version": "1.6.4-release-helper",
  "build_sha": "...",
  "build_short_sha": "...",
  "build_date": "...",
  "size": 1234567,
  "firmware_url": "https://raw.githubusercontent.com/pequestick/Pequestick-ESP32-Boia_Piscina/main/firmware/firmware.bin",
  "notes": "Build automatica de GitHub Actions"
}
```

La pantalla OTA mostra:

- Build actual.
- Estat d'Internet.
- Estat GitHub / manifest.
- Versió remota.
- SHA remota.
- Si hi ha actualització disponible.
- Si la versió remota és més antiga.

---

## GitHub Actions

El repo inclou un workflow a:

```text
.github/workflows/build-firmware.yml
```

Quan es fa push a `main`, GitHub Actions:

1. Instal·la PlatformIO.
2. Compila el firmware.
3. Copia `firmware.bin` a `firmware/firmware.bin`.
4. Genera `firmware/manifest.json`.
5. Fa commit automàtic dels artefactes OTA.

La versió del manifest es llegeix automàticament des de:

```cpp
const char* FIRMWARE_VERSION = "...";
```

a `src/AppConfig.cpp`.

---

## Flux de treball correcte

El projecte real és la carpeta clonada del repo:

```text
C:\Users\peque\Documents\PlatformIO\Projects\Pequestick-ESP32-Boia_Piscina
```

Quan es rep una nova versió en ZIP, no s'ha d'obrir com un projecte nou. Cal copiar només els fitxers indicats dins d'aquesta carpeta real.

No copiar mai:

```text
.pio/
build/
.vscode/
firmware/
```

La carpeta `firmware/` la gestiona GitHub Actions.

Flux habitual:

```powershell
git status
git add .
git commit -m "vX.Y.Z descripcio"
git push origin main
```

Després revisar:

```text
GitHub -> Actions
```

Quan l'Action acaba en verd, la boia pot comprovar actualització OTA des de GitHub.

---

## Fitxers que no s'han de pujar

El `.gitignore` ha d'evitar pujar fitxers generats:

```gitignore
.pio/
build/

.vscode/.browse.c_cpp.db
.vscode/c_cpp_properties.json
.vscode/launch.json
.vscode/ipch/

*.bin
*.elf
*.map
*.hex

.DS_Store
Thumbs.db
```

---

## Preparació física de la boia

Material físic actual:

- Tub PVC 50 mm.
- Taps.
- Protoboard per proves.
- Sonda DS18B20.
- Battery shield i bateries pendents/provats.
- SHT41 previst.

Recomanacions:

- Primer provar estanqueïtat amb el tub buit.
- Fer prova amb paper absorbent dins.
- No tancar electrònica fins provar OTA diverses vegades.
- Posar silica gel dins la boia.
- No posar el SHT41 enganxat al silica gel ni a reguladors calents.
- Deixar l'antena Wi-Fi de l'ESP32 el més lliure possible.

---

## Silica gel

És recomanable posar una bosseta petita de silica gel dins la boia.

Objectiu:

- Reduir humitat interna.
- Evitar condensació.
- Protegir connectors i electrònica.

No substituirà un bon segellat. És una assegurança, no una solució màgica.

---

## Roadmap proper

Funcionalitats previstes:

- Integració funcional del SHT41.
- Lectura de bateria.
- Mode energia: instal·lació / producció / bateria.
- LEDs més austers per bateria.
- Alarmes d'humitat interna.
- Alarmes de bateria baixa.
- Watchdog i autorecuperació.
- Logs interns curts.
- Perfils de hardware per DevKitC-1 i ESP32-C6 Mini.

---

## Històric resumit de versions

### v0.7

- Wi-Fi configurable.
- AP de rescat.

### v0.8

- MQTT configurable.

### v0.8.2

- Passwords visibles/ocultables amb botó d'ull.

### v0.9

- Home Assistant Discovery.

### v1.0

- Controls bàsics des de Home Assistant.

### v1.1

- OTA manual.
- Sonda robusta.
- Xarxa avançada.

### v1.2

- Diagnòstic.
- Botó físic.
- LED d'estat.
- Mapa GPIO a la web.

### v1.3

- Rentat de cara de la web.
- WebSocket.
- Nom dispositiu i hostname.

### v1.4

- Manteniment.
- Backup/import/export.
- Centre d'ajuda.
- Subpàgines i menú lateral.

### v1.5

- Històric de temperatura des de Home Assistant.
- Configuració d'hores mostrades.
- Mostres horàries.

### v1.6.0

- OTA des de GitHub.
- GitHub Actions.
- Manifest OTA.

### v1.6.1

- Comprovació d'accés a Internet.
- Correccions d'URL raw de GitHub.

### v1.6.2

- Manifest amb versió automàtica llegida des d'`AppConfig.cpp`.


### v1.6.4

- Afegides constants `FIRMWARE_CHANGE_TITLE` i `FIRMWARE_CHANGE_NOTES` a `AppConfig.cpp`.
- Afegit `tools/release.ps1` per fer commit, rebase i push llegint el missatge de release directament del firmware.
- GitHub Actions publica `notes` al manifest OTA a partir de `FIRMWARE_CHANGE_NOTES`.

### v1.6.3

- Pantalla OTA professional.
- Estat Internet/GitHub/actualització.
- Bloqueig de downgrade.
- Resultats visibles a la mateixa pàgina.

---

## Nota de seguretat

No pujar mai al repo:

- Password Wi-Fi.
- Password MQTT.
- Token de Home Assistant.
- Tokens GitHub.
- Credencials personals.

Les credencials han de quedar guardades a la memòria de l'ESP32 mitjançant la web local, no dins del codi font.
