# inferencia-en-placa

Proyecto basado en PlatformIO para un ESP32-S3 con cámara que ejecuta inferencia de IA y publica resultados vía MQTT.

## Descripción

Este proyecto configura un dispositivo ESP32-S3 con cámara para ejecutar el modelo de detección generado en Edge Impulse y publicar el resultado vía MQTT.

El objetivo central del despliegue es validar en hardware real el modelo entrenado en Edge Impulse, con acceso a la cámara, conexión WiFi y transmisión de inferencias hacia el sistema de monitoreo central.

Incluye:

- cámara OV5640 / compatible
- conexión WiFi
- captura de frames y preparación del input del modelo
- ejecución de inferencia con el runtime del modelo de Edge Impulse
- publicación de resultados vía MQTT
- envío de estado del dispositivo
- recepción de comandos desde el panel central

La lógica principal está en `src/main.cpp` y la configuración del tablero en `platformio.ini`.

## Motor de inferencia

Este despliegue no usa el motor EON. La exportación de Edge Impulse incluida en este proyecto se integra con el runtime TFLite del SDK de Edge Impulse, como se indica en los metadatos generados del modelo (`EI_CLASSIFIER_INFERENCING_ENGINE` = `EI_CLASSIFIER_TFLITE`, `EI_CLASSIFIER_COMPILED` = `1`).

Es decir, el modelo se ejecuta como una implementación compilada de TensorFlow Lite Micro dentro del SDK de Edge Impulse, no como un modelo EON específico.

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

## Configuración

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

## Compilar y subir

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
- ejecuta la inferencia del modelo exportado desde Edge Impulse usando el runtime TFLite del SDK
- publica el resultado en `MQTT_TOPIC_INF`
- publica salud del dispositivo en `MQTT_TOPIC_STATUS`
- escucha comandos entrantes en `MQTT_TOPIC_CMD`

Este flujo está diseñado para probar en placa el modelo generado en Edge Impulse y observar su comportamiento en tiempo real, sin depender de un motor EON.

## Diagnóstico

Si el proyecto no compila o no conecta, revisa:

- que el puerto COM de la placa sea correcto
- que la versión de la cámara y pines coincidan con el hardware
- que `.env` exista y contenga valores válidos
- que el broker MQTT esté accesible desde la red del ESP32

## Nota

Este firmware está diseñado para trabajar junto con un panel de visualización MQTT, como el proyecto `Panel-andon` de este workspace.
