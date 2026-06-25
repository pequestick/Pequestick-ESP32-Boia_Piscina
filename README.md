# Pequestick ESP32 Boia Piscina

Firmware per a una boia de piscina basada en **ESP32-C6**, pensada per mesurar la temperatura de l'aigua, integrar-se amb **Home Assistant** via MQTT i poder-se mantenir per Wi-Fi mitjançant OTA.

El projecte ha evolucionat des d'una prova simple amb una sonda **DS18B20** fins a una plataforma IoT força completa amb web pròpia, configuració persistent, MQTT, Home Assistant Discovery, OTA manual, OTA des de GitHub, diagnòstic, configuració de xarxa i preparació per bateria i sensors interns.

---

## Estat actual

Versió actual documentada:

```text
1.21.0-header-actions
```

Funcionalitats principals actuals:

- ESP32-C6 amb web de configuració local.
- Lectura de temperatura d'aigua amb DS18B20.
- Wi-Fi configurable des de la web.
- Cercador de xarxes Wi-Fi properes amb selecció d'SSID, intensitat de senyal i estat de seguretat.
- Retorn automàtic a DHCP quan es canvia l'SSID, evitant heretar una IP fixa de la xarxa anterior.
- Botons independents per guardar les credencials Wi-Fi i la configuració avançada de xarxa.
- Mode AP de rescat si no pot connectar a la xarxa.
- MQTT configurable.
- Home Assistant Discovery.
- Controls bàsics des de Home Assistant.
- OTA manual via web.
- OTA des de GitHub mitjançant `firmware/manifest.json`.
- Comprovació d'accés a Internet des de la boia.
- Pantalla OTA millorada amb estat Internet, GitHub, versió remota i actualització disponible.
- OTA GitHub assistida pel navegador, amb descàrrega ràpida, pujada local i verificació SHA-256 a la boia.
- Web amb menú lateral, subpàgines i estructura més professional.
- Botons globals al header per reiniciar la boia i sortir/tancar sessió.
- Botó de guardar independent a la secció de lectura de la sonda.
- Configuració exportable/importable.
- Sensor intern SHT41 actiu.
- Lectura de bateria per GPIO1 amb divisor resistiu 100k/100k, tensió estimada i percentatge aproximat.
- Configuració web dels volts de bateria buida/plena, percentatge LOW i calibratge ADC.
- microSD per SPI amb pàgina pròpia separada en subpàgines, estat de muntatge, espai ocupat, explorador tipus Windows, visor modal, descàrrega CSV i neteja lògica.
- Guardat local d'històric de lectures separat per dies a `/boia/history/YYYY-MM-DD.csv`.
- Pàgina inicial amb bateria i uptime en format compacte dins el footer del gràfic per no tapar la visualització.
- Estadístiques locals precalculades a `/boia/stats/daily_snapshots.csv`.
- Logs locals a `/boia/logs/YYYY-MM-DD.log`.
- Buffer MQTT offline a `/boia/mqtt/pending.jsonl`, pensat perquè Home Assistant no perdi lectures quan la xarxa o el broker cauen.
- Snapshot de configuració, fitxer de versió i blackbox d'arrencada a la SD.
- Petit explorador/visualitzador web de fitxers SD.
- Accés web protegit amb usuari i contrasenya persistents.
- Lectura pública de temperatura i estat de sensors des de la pantalla d'accés.
- Sessió web persistent de set dies amb cookie versionada per navegar sense reautenticacions constants.
- OTA GitHub validada amb TLS i SHA-256.

### Primer accés a la web

Després d'instal·lar aquesta versió o de fer un reset de fàbrica:

```text
Usuari:     admin
Contrasenya: 1234
```

La web obliga a canviar aquestes credencials abans de permetre accedir a la resta del sistema. El nou usuari i el hash PBKDF2 de la contrasenya es guarden a NVS mitjançant `Preferences`.

Les credencials Wi-Fi, MQTT i Home Assistant introduïdes des de la web també es mantenen a NVS. Els formularis les mostren emmascarades i permeten veure-les temporalment amb el botó de l'ull.

---

## Hardware actual

### Placa principal

- ESP32-C6 DevKitC-1 durant desenvolupament.
- Preparació futura per migrar a placa ESP32-C6 Mini.

### Sensors

- **DS18B20 impermeable** per temperatura de l'aigua.
- **SHT41** actiu per temperatura i humitat internes de la boia.

### microSD

Mòdul microSD SPI configurat per guardar històric local de lectures.

Connexió recomanada del mòdul de la foto:

```text
Mòdul microSD  -> ESP32-C6
3V3 / VCC      -> 3V3
GND            -> GND
CS             -> GPIO18
MOSI           -> GPIO19
CLK / SCK      -> GPIO21
MISO           -> GPIO20
```

Punts importants:

- Alimenta el mòdul a **3V3**, no a 5V, perquè l'ESP32-C6 no és tolerant a senyals de 5V als GPIO.
- Si el teu mòdul només accepta 5V segons la serigrafia real, no el connectis a cegues: cal confirmar que porta regulador i adaptació de nivells. Si no, és una manera ràpida de matar l'ESP32.
- GND del mòdul i GND de l'ESP32 han d'anar units.
- La targeta ha d'estar en FAT32. Si la boia no la munta, formata-la primer al PC.
- Fes servir cables curts. SPI amb Dupont llarg fa coses rares.

Constants principals:

```cpp
#define SD_SPI_MOSI_PIN 19
#define SD_SPI_CLK_PIN 21
#define SD_SPI_MISO_PIN 20
#define SD_SPI_CS_PIN 18
const bool SD_CARD_ENABLED = true;
const uint32_t SD_SPI_FREQUENCY_HZ = 1000000;
const char* SD_HISTORY_DAILY_DIR = "/boia/history";
const char* SD_DAILY_STATS_FILE = "/boia/stats/daily_snapshots.csv";
const char* SD_MQTT_PENDING_FILE = "/boia/mqtt/pending.jsonl";
```

La web afegeix la pàgina **SD / Històric** separada en subpàgines: **Estat**, **Dades**, **Mapa fitxers**, **Últim registre**, **Explorador** i **Manteniment**. Des d'aquí es pot veure l'estat, l'espai ocupat, descarregar CSV, consultar estadístiques precalculades, revisar logs, veure el buffer MQTT pendent i navegar pels fitxers de la targeta.

El botó de neteja no és un format físic complet: esborra els fitxers de la microSD i recrea l'estructura `/boia`. Si la targeta està corrupta, format FAT32 al PC i prou romanços.

### Energia

De moment es treballa amb alimentació USB / externa, però el firmware ja pot llegir la tensió real de bateria si es connecta el punt BAT+ al GPIO1 mitjançant divisor resistiu.

Connexió de mesura de bateria implementada:

```text
BAT+ bateria ----[ 100 kΩ ]----+---- GPIO1 / ADC bateria
                               |
                             [ 100 kΩ ]
                               |
GND bateria -------------------+---- GND ESP32
```

Punts importants, perquè aquí no hi ha marge per fer el bèstia:

- No connectis mai BAT+ directament al GPIO1.
- Amb dues resistències iguals de 100 kΩ el divisor és x2: l'ESP32 veu aproximadament la meitat de la tensió de bateria.
- El càlcul de percentatge està pensat per una bateria Li-Ion/LiPo 1S i ara es pot ajustar des de **Sistema → Bateria**. Per defecte: 3.00 V = 0 %, 4.20 V = 100 %, LOW per sota del 15 %. És una estimació lineal, no un indicador perfecte d'estat de càrrega.
- Si la web no coincideix amb el multímetre, ajusta el **factor de calibratge ADC**. Exemple: si el multímetre diu 3.80 V i la web diu 3.60 V, factor aproximat = 3.80 / 3.60 = 1.056.
- Si el muntatge final és 2S, 5 V boost, LiFePO4 o una altra química, caldrà canviar rangs i possiblement el divisor. No ho barregis sense revisar-ho.

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

## Connexió SHT41

El SHT41 farà de sensor ambiental intern dins la boia:

```text
SHT41 VCC -> 3V3
SHT41 GND -> GND
SHT41 SDA -> GPIO6
SHT41 SCL -> GPIO7
```

En una futura ESP32-C6 Mini, la proposta inicial és reservar:

```text
SDA -> GPIO8
SCL -> GPIO9
```

Això encara s'haurà de validar amb el pinout real de la placa final.

---

## Mesura de bateria per GPIO1

El firmware llegeix la bateria al **GPIO1**. La lectura es fa amb `analogReadMilliVolts()`, mitjana de 16 mostres i atenuació `ADC_11db`. Després aplica el factor del divisor:

```text
voltatge_bateria = voltatge_gpio1 * 2.0 * factor_calibratge_adc
```

Constants principals a `src/AppConfig.cpp` / `include/AppConfig.h`:

```cpp
#define BATTERY_VOLTAGE_ADC_PIN 1
const float BATTERY_DIVIDER_RATIO = 2.0f;
const float DEFAULT_BATTERY_CALIBRATION_FACTOR = 1.0f;
const float DEFAULT_BATTERY_EMPTY_VOLTAGE = 3.00f;
const float DEFAULT_BATTERY_FULL_VOLTAGE = 4.20f;
const float DEFAULT_BATTERY_LOW_PERCENT = 15.0f;
const uint8_t BATTERY_ADC_SAMPLES = 16;
```

La web mostra bateria a la pantalla d'estat, al login públic, al diagnòstic, al mapa de hardware i a **Sistema → Bateria**. En aquesta pàgina es poden configurar:

- Voltatge de bateria buida, que equival a 0 %.
- Voltatge de bateria plena, que equival a 100 %.
- Percentatge a partir del qual l'estat passa a `LOW`.
- Factor de calibratge ADC.

MQTT publica `battery_voltage`, `battery_percent` i `battery_status`, i Home Assistant Discovery crea les entitats corresponents.

Una lectura de **3.02 V** en una Li-Ion 1S no és “mitja bateria”: és pràcticament buida. Pot continuar funcionant una estona, sobretot si hi ha conversor o regulador, però no és un estat saludable per anar drenat la bateria cada dia.

---

## Històric local a microSD

A partir de la versió `1.18.0-sd-blackbox`, la SD deixa de ser només un CSV i passa a ser una **caixa negra local opcional**. La boia continua funcionant sense SD, però si la targeta existeix tot queda registrat localment.

Estructura creada automàticament:

```text
/boia/history/YYYY-MM-DD.csv          -> detall de lectures, un fitxer per dia
/boia/stats/daily_snapshots.csv       -> estadístiques precalculades del dia
/boia/logs/YYYY-MM-DD.log             -> logs de sistema
/boia/mqtt/pending.jsonl              -> buffer MQTT offline
/boia/config/config_snapshot.json     -> còpia llegible de la configuració
/boia/system/version.json             -> versió instal·lada
/boia/blackbox/last_boot.json         -> caixa negra de l'última arrencada
/boia/calibration/                    -> reservat per calibratges futurs
```

Cada cicle de lectura escriu una línia CSV al fitxer del dia:

```text
/boia/history/2026-06-25.csv
```

Si encara no hi ha hora NTP, el firmware escriu provisionalment a:

```text
/boia/history/boot.csv
```

Columnes del detall:

```text
unix_time,iso_time,uptime_seconds,water_temperature_c,raw_temperature_c,water_sensor_status,internal_temperature_c,internal_humidity_percent,internal_env_status,battery_voltage_v,battery_percent,battery_status,wifi_rssi_dbm
```

Notes clares:

- Si la boia encara no ha sincronitzat hora per NTP, `unix_time` i `iso_time` poden quedar buits. `uptime_seconds` sempre queda escrit.
- El fitxer és append-only: no reescriu l'històric cada cop, només afegeix una línia.
- Les estadístiques precalculades no substitueixen el detall; serveixen perquè la web o Home Assistant puguin llegir resums sense processar tot l'històric.
- `/sd-history.csv` descarrega el CSV del dia actual.
- `/sd-daily-stats.csv` descarrega els snapshots precalculats.
- `/sd-info` retorna l'estat de la SD en JSON.
- `/sd-list?path=/boia` retorna un llistat JSON del directori.
- `/sd-view?path=/boia/logs/boot.log` mostra un fitxer de text/CSV/JSON des de la web.
- `/sd-download?path=/boia/history/YYYY-MM-DD.csv` descarrega qualsevol fitxer de la SD.

### Buffer MQTT per Home Assistant

Quan MQTT està activat però el broker no està connectat, la boia no llença la telemetria: la guarda a:

```text
/boia/mqtt/pending.jsonl
```

Quan MQTT torna, el firmware intenta buidar aquest buffer i publica les línies pendents a:

```text
boia_piscina/telemetry_buffer
```

La telemetria normal en temps real continua anant a:

```text
boia_piscina/telemetry
```

Això no és una base de dades perfecta ni un substitut de Home Assistant, però és molt millor que perdre lectures per una caiguda de Wi-Fi, broker o HA. Si vols fer-ho encara més bèstia més endavant, el següent pas serà que Home Assistant llegeixi directament aquests fitxers via HTTP i importi el detall.

### Explorador de fitxers SD

La pàgina **SD / Històric** inclou un explorador senzill. Permet:

- Entrar a carpetes.
- Veure fitxers `.csv`, `.json`, `.jsonl`, `.log` i `.txt`.
- Descarregar qualsevol fitxer.
- Revisar logs i blackbox sense treure la targeta de la boia.

No és un gestor de fitxers complet. No edita fitxers, no fa base de dades i no ha de ser crític per arrencar. La SD és una capa extra: si falla, la boia ha de seguir viva.

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
- SD / Històric.
- JSON d'estat.

La interfície utilitza WebSocket per actualitzar estat en directe quan és possible.

---

## Wi-Fi i AP de rescat

Si la boia no pot connectar a la xarxa configurada, entra en mode AP de rescat.

Dades per defecte:

```text
SSID AP: BOIA-PISCINA-SETUP
IP AP:   http://192.168.4.1
Password: boia-xxxxxx (últims 6 caràcters de la MAC, també visible pel monitor sèrie)
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
boia_piscina/battery_voltage
boia_piscina/battery_percent
boia_piscina/battery_status
boia_piscina/rssi
boia_piscina/uptime
boia_piscina/ip
boia_piscina/total_reads
boia_piscina/valid_reads
boia_piscina/failed_reads
boia_piscina/telemetry
boia_piscina/telemetry_buffer
boia_piscina/sd_status
boia_piscina/sd_history_file
boia_piscina/sd_mqtt_pending
```

També accepta comandes MQTT per accions com reinici i republicació de Discovery.

---

## Home Assistant

El firmware suporta Home Assistant Discovery.

Entitats principals:

```text
sensor.boia_piscina_temperature
sensor.boia_piscina_internal_temperature
sensor.boia_piscina_internal_humidity
sensor.boia_piscina_battery_voltage
sensor.boia_piscina_battery
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
  "version": "1.6.5-ota-progress-ui",
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
- SHT41 actiu a l'adreça I2C `0x44`.

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

- Alarmes configurables de condensació i humitat elevada a partir de l'SHT41.
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

### v1.21.0

- Afegeix botons globals al header per **Reiniciar boia** i **Sortir / tancar sessió**.
- Separa millor la configuració de temperatura: la subpàgina **Lectura** ara té el seu propi botó **Guardar lectura** per aplicar interval i decimals sense haver d'anar a calibratge.
- La subpàgina **Calibratge** manté el seu propi botó **Guardar calibratge i sonda** per offset i rang vàlid.

### v1.20.0

- Separa la pàgina **SD / Històric** en subpàgines amb submenú lateral: Estat, Dades, Mapa fitxers, Últim registre, Explorador i Manteniment.
- Manté l'explorador tipus Windows dins la subpàgina **Explorador**, sense canviar de pàgina per entrar a carpetes i amb visor modal per fitxers.
- Compacta les etiquetes de bateria i uptime de la pàgina inicial perquè no tapin el gràfic.
- Mou bateria i uptime al footer del gràfic, a l'esquerra dels botons de resolució/rang, deixant els botons centrats.
- Manté les línies horitzontals de referència i els textos de mínim/màxim a la dreta del gràfic.

### v1.19.0

- Canvia l'explorador SD a una interfície tipus Windows sense recarregar pàgina per entrar a carpetes.
- Obre fitxers en una finestra modal amb endpoint `/sd-read?path=...` i manté la descàrrega directa amb `/sd-download?path=...`.
- Afegeix botons ràpids per `/boia`, històric, estadístiques, logs, buffer MQTT, config, blackbox i sistema.
- Mostra el temps engegada / uptime a la pàgina inicial.
- Mou els textos de mínim i màxim del gràfic cap a la dreta.
- Afegeix línies horitzontals de referència al gràfic de temperatura.
- Reforça el llistat JSON de la SD perquè les rutes siguin consistents encara que `file.path()` retorni noms relatius.

### v1.18.0

- Converteix la microSD en caixa negra local opcional.
- Guarda històrics separats per dies a `/boia/history/YYYY-MM-DD.csv`.
- Afegeix estadístiques precalculades a `/boia/stats/daily_snapshots.csv`.
- Afegeix logs locals, blackbox d'arrencada, snapshot de configuració i fitxer de versió.
- Afegeix buffer MQTT offline a `/boia/mqtt/pending.jsonl` i replay a `telemetry_buffer` quan torna el broker.
- Afegeix explorador i visualitzador web de fitxers SD.
- Canvia pins SD a CS GPIO18, MOSI GPIO19, SCK GPIO21, MISO GPIO20 i baixa SPI a 1 MHz.

### v1.17.0

- Afegeix pàgina **Sistema → Bateria**.
- Permet configurar volts de bateria buida i plena.
- Permet ajustar percentatge LOW i calibratge ADC.
- Canvia el valor per defecte de bateria buida a 3.00 V.
- Evita el solapament de la fitxa de bateria amb els controls del gràfic inicial.

### v1.16.0

- Afegeix suport microSD per SPI.
- Guarda històric local de lectures a `/boia/history.csv`.
- Nova pàgina web SD / Històric amb estat, espai ocupat, descàrrega CSV i neteja lògica.
- Nou endpoint `/sd-info` i descàrrega directa `/sd-history.csv`.

### v1.15.0

- Lectura de bateria per GPIO1 amb divisor 100k/100k.
- Mostra tensió, percentatge i estat de bateria a la web.
- Publica bateria per MQTT i Home Assistant Discovery.

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


### v1.6.9

- Canvia la descàrrega OTA des de GitHub a blocs HTTP Range de 32 KB.
- Si GitHub/TLS deixa d'enviar dades, la boia tanca la connexió i continua des del byte exacte on s'havia quedat.
- Millora el log OTA amb informació de rangs i reconnexions.
- Redueix el risc de timeout en actualitzacions grans descarregades des de `raw.githubusercontent.com`.

### v1.6.8

- Millora la lectura del flux TLS durant OTA des de GitHub.
- Força HTTP/1.0 i amplia logs de descàrrega per diagnosticar talls de connexió.

### v1.6.7

- La pantalla OTA comprova automàticament Internet i GitHub quan s'obre.
- Les targetes d'Internet, GitHub i Actualització s'actualitzen a la mateixa pàgina.
- El log OTA queda ocult mentre només es fan comprovacions; només apareix durant una actualització real o després d'un resultat OTA.
- La comprovació de manifest ja no embruta el log OTA.

### v1.6.6

- Afegeix log OTA en directe per veure cada pas del procés d'actualització.
- Mostra missatges de diagnòstic per Serial i WebSocket.
- Afegeix timeout explícit quan no arriben dades del firmware.

### v1.6.5

- Afegeix una barra de progrés a Manteniment → OTA.
- L'OTA local mostra el progrés de pujada del `firmware.bin` des del navegador.
- L'OTA des de GitHub mostra fase, percentatge, bytes descarregats i estat en directe via WebSocket mentre la boia descarrega i escriu el firmware.
- El formulari d'instal·lació GitHub ja no deixa la pantalla muda: mostra que està descarregant, verificant i reiniciant.

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
