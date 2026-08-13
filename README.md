# inferencia-en-placa

Proyecto basado en PlatformIO para una placa ESP32-S3 con cámara que ejecuta inferencia de IA y publica resultados vía protocolo MQTT.

El desarrollo fue en conjunto entre:
- [Prof. Juan Carlos Acuña](https://github.com/jucaacuna)
- [Prof. Martín Derly Bentancor](https://github.com/bentancormartin)
- Prof. Gustavo L. Farías

## Descripción

Este proyecto configura un dispositivo ESP32-S3 con cámara para ejecutar el modelo de detección generado en Edge Impulse. De manera complementaria, se configura una conexión WiFi y se publica el resultado de las inferencias vía MQTT hacia un broker.

Incluye:

- cámara OV5640 / compatible
- conexión WiFi
- captura de frames y preparación del input del modelo
- ejecución de inferencia con el runtime del modelo de Edge Impulse
- publicación de resultados vía MQTT
- envío de estado del dispositivo
- recepción de comandos desde el panel central

La lógica principal está en `src/main.cpp`, la configuración del tablero en `platformio.ini`.

## Motor de inferencia

Este proyecto usa la exportación de Edge Impulse descargada como Arduino library y compilada con el motor EON.

La carpeta `lib/Tarea_IoT_grupo_inferencing/` contiene la librería Arduino exportada desde Edge Impulse, con su SDK (`edge-impulse-sdk`) y el modelo optimizado para ejecutarse en microcontroladores. El modelo se integra con el runtime del SDK de Edge Impulse, usando la ruta de inferencia EON/compiled que genera el compilador de Edge Impulse para embedded devices.

En otras palabras, esta no es una exportación genérica de C++ para Linux ni una librería “manual”; es la librería Arduino oficial del modelo generado en Edge Impulse, preparada para ESP32-S3 y optimizada por EON Compiler.

## Estructura

- `src/main.cpp`: firmware principal del ESP32
- `lib/`: librerías, SDK y modelo de Edge Impulse
- `include/`: cabeceras adicionales
- `load_env.py`: script que carga variables de entorno al compilar
- `.env.example`: ejemplo de configuración
- `platformio.ini`: configuración del dispositivo y compilación

## Requisitos

- PlatformIO
- VS Code (opcional, recomendado)
- ESP32-S3 con cámara compatible
- Broker MQTT accesible desde la red
- Red WiFi con SSID y contraseña válidas

## Configuración (paso a paso)

1. Copia el archivo de ejemplo:

   ```bash
   copy .env.example .env
   ```

2. Completa tus credenciales y tópicos MQTT:

   ```env
   WIFI_SSID=tu_red
   WIFI_PASS=tu_password
   MQTT_BROKER=broker.hivemq.com
   MQTT_PORT=1883
   MQTT_TOPIC_INF=tu_prefijo/inference
   MQTT_TOPIC_STATUS=tu_prefijo/status
   MQTT_TOPIC_CMD=tu_prefijo/cmd
   ```

3. Ajusta los pines de la cámara si es necesario en `platformio.ini`.

## Compilar y subir a la placa

```bash
pio run
pio run --target upload
```

También puedes usar la acción de PlatformIO desde VS Code.

## Funcionamiento

El firmware:

- se conecta a WiFi
- inicializa la cámara
- prepara el frame para el input del modelo
- ejecuta la inferencia del modelo exportado desde Edge Impulse como Arduino library con el runtime del SDK y el modelo EON
- publica el resultado en `MQTT_TOPIC_INF`
- publica salud del dispositivo en `MQTT_TOPIC_STATUS`
- escucha comandos entrantes en `MQTT_TOPIC_CMD`

Este flujo está diseñado para probar en placa el modelo generado en Edge Impulse, descargado como librería Arduino y optimizado por EON Compiler para ejecutar inferencias en ESP32-S3.

## Diagnóstico

Si el proyecto no compila o no conecta, revisa:

- que el puerto COM de la placa sea correcto
- que la versión de la cámara y pines coincidan con el hardware
- que `.env` exista y contenga valores válidos
- que el broker MQTT esté accesible desde la red del ESP32

## Nota

Este firmware está diseñado para trabajar junto con un panel de visualización que consuma datos desde el broker MQTT, como el proyecto [`Panel-andon`](https://github.com/jucaacuna/Panel-andon).
