#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "esp_camera.h"

#define APP_PRINTLN(msg) do { Serial.println(msg); Serial0.println(msg); } while (0)
#define APP_PRINTF(...) do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while (0)

#if __has_include(<edge-impulse-sdk/classifier/ei_run_classifier.h>)
#include <edge-impulse-sdk/classifier/ei_run_classifier.h>
#define EI_LIB_AVAILABLE 1
#else
#define EI_LIB_AVAILABLE 0
#endif

// MQTT topics — defined via .env → load_env.py → build flags
#ifndef MQTT_TOPIC_INF
#define MQTT_TOPIC_INF "esp32s3/inference"
#endif
#ifndef MQTT_TOPIC_STATUS
#define MQTT_TOPIC_STATUS "esp32s3/status"
#endif
#ifndef MQTT_TOPIC_CMD
#define MQTT_TOPIC_CMD "esp32s3/cmd"
#endif

// Set these pins for your exact ESP32-S3 + OV5640 board.
// You can define them in platformio.ini build_flags as -D CAMERA_PIN_XXX=<value>.
#ifndef CAMERA_PIN_PWDN
#define CAMERA_PIN_PWDN -1
#endif
#ifndef CAMERA_PIN_RESET
#define CAMERA_PIN_RESET -1
#endif
#ifndef CAMERA_PIN_XCLK
#define CAMERA_PIN_XCLK -1
#endif
#ifndef CAMERA_PIN_SIOD
#define CAMERA_PIN_SIOD -1
#endif
#ifndef CAMERA_PIN_SIOC
#define CAMERA_PIN_SIOC -1
#endif
#ifndef CAMERA_PIN_D7
#define CAMERA_PIN_D7 -1
#endif
#ifndef CAMERA_PIN_D6
#define CAMERA_PIN_D6 -1
#endif
#ifndef CAMERA_PIN_D5
#define CAMERA_PIN_D5 -1
#endif
#ifndef CAMERA_PIN_D4
#define CAMERA_PIN_D4 -1
#endif
#ifndef CAMERA_PIN_D3
#define CAMERA_PIN_D3 -1
#endif
#ifndef CAMERA_PIN_D2
#define CAMERA_PIN_D2 -1
#endif
#ifndef CAMERA_PIN_D1
#define CAMERA_PIN_D1 -1
#endif
#ifndef CAMERA_PIN_D0
#define CAMERA_PIN_D0 -1
#endif
#ifndef CAMERA_PIN_VSYNC
#define CAMERA_PIN_VSYNC -1
#endif
#ifndef CAMERA_PIN_HREF
#define CAMERA_PIN_HREF -1
#endif
#ifndef CAMERA_PIN_PCLK
#define CAMERA_PIN_PCLK -1
#endif

#if EI_LIB_AVAILABLE
static constexpr int kModelWidth = EI_CLASSIFIER_INPUT_WIDTH;
static constexpr int kModelHeight = EI_CLASSIFIER_INPUT_HEIGHT;
#else
static constexpr int kModelWidth = 96;
static constexpr int kModelHeight = 96;
#endif

static WiFiClient   s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);

static void process_command_line(const String &lineRaw);

static uint8_t  *s_resizedRgb888      = nullptr;
static uint32_t  s_lastHeartbeatMs    = 0;
static uint32_t  s_lastMqttAttemptMs  = 0;
static bool      s_streamEnabled      = true;
static uint32_t  s_inferenceIntervalMs = 500;
static String    s_serialCmdBuffer;

static bool camera_pins_are_configured() {
  return CAMERA_PIN_XCLK >= 0 && CAMERA_PIN_SIOD >= 0 && CAMERA_PIN_SIOC >= 0 &&
         CAMERA_PIN_D0 >= 0 && CAMERA_PIN_D1 >= 0 && CAMERA_PIN_D2 >= 0 &&
         CAMERA_PIN_D3 >= 0 && CAMERA_PIN_D4 >= 0 && CAMERA_PIN_D5 >= 0 &&
         CAMERA_PIN_D6 >= 0 && CAMERA_PIN_D7 >= 0 && CAMERA_PIN_VSYNC >= 0 &&
         CAMERA_PIN_HREF >= 0 && CAMERA_PIN_PCLK >= 0;
}

static inline void rgb565_to_rgb888(uint16_t p, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
  g = static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63);
  b = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
}

static void append_json_escaped(String &out, const char *text) {
  if (text == nullptr) {
    out += "null";
    return;
  }

  out += '"';
  for (const char *p = text; *p != '\0'; ++p) {
    switch (*p) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += *p; break;
    }
  }
  out += '"';
}

static void resize_center_crop_rgb565_to_rgb888(const uint8_t *src,
                                                 int srcW,
                                                 int srcH,
                                                 uint8_t *dst,
                                                 int dstW,
                                                 int dstH) {
  int crop = (srcW < srcH) ? srcW : srcH;
  int x0 = (srcW - crop) / 2;
  int y0 = (srcH - crop) / 2;

  for (int y = 0; y < dstH; y++) {
    int sy = y0 + (y * crop) / dstH;
    for (int x = 0; x < dstW; x++) {
      int sx = x0 + (x * crop) / dstW;
      int srcIndex = (sy * srcW + sx) * 2;
      uint16_t p = static_cast<uint16_t>(src[srcIndex]) |
                   (static_cast<uint16_t>(src[srcIndex + 1]) << 8);

      uint8_t r;
      uint8_t g;
      uint8_t b;
      rgb565_to_rgb888(p, r, g, b);

      int dstIndex = (y * dstW + x) * 3;
      dst[dstIndex] = r;
      dst[dstIndex + 1] = g;
      dst[dstIndex + 2] = b;
    }
  }
}

#if EI_LIB_AVAILABLE
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t pixelOffset = offset * 3;
  for (size_t i = 0; i < length; i++) {
    size_t base = pixelOffset + (i * 3);
    out_ptr[i] = (static_cast<uint32_t>(s_resizedRgb888[base]) << 16) |
                 (static_cast<uint32_t>(s_resizedRgb888[base + 1]) << 8) |
                 static_cast<uint32_t>(s_resizedRgb888[base + 2]);
  }
  return 0;
}
#endif

static bool init_camera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAMERA_PIN_D0;
  config.pin_d1 = CAMERA_PIN_D1;
  config.pin_d2 = CAMERA_PIN_D2;
  config.pin_d3 = CAMERA_PIN_D3;
  config.pin_d4 = CAMERA_PIN_D4;
  config.pin_d5 = CAMERA_PIN_D5;
  config.pin_d6 = CAMERA_PIN_D6;
  config.pin_d7 = CAMERA_PIN_D7;
  config.pin_xclk = CAMERA_PIN_XCLK;
  config.pin_pclk = CAMERA_PIN_PCLK;
  config.pin_vsync = CAMERA_PIN_VSYNC;
  config.pin_href = CAMERA_PIN_HREF;
  config.pin_sccb_sda = CAMERA_PIN_SIOD;
  config.pin_sccb_scl = CAMERA_PIN_SIOC;
  config.pin_pwdn = CAMERA_PIN_PWDN;
  config.pin_reset = CAMERA_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, 0);
    sensor->set_hmirror(sensor, 0);
  }

  return true;
}

// ── WiFi ────────────────────────────────────────────────────────────────────

static void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  APP_PRINTLN("WiFi: connecting...");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    APP_PRINTF("WiFi: connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    APP_PRINTLN("WiFi: failed, running offline.");
  }
}

// ── MQTT ────────────────────────────────────────────────────────────────────

static void mqtt_on_cmd(char *topic, byte *payload, unsigned int len) {
  String cmd;
  cmd.reserve(len);
  for (unsigned int i = 0; i < len; i++) {
    cmd += static_cast<char>(payload[i]);
  }
  process_command_line(cmd);
}

static void mqtt_reconnect() {
  if (s_mqtt.connected() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (millis() - s_lastMqttAttemptMs < 5000) {
    return;
  }
  s_lastMqttAttemptMs = millis();

  String clientId = "esp32s3-" + WiFi.macAddress();
  APP_PRINTF("MQTT: connecting to %s:%d ...\n", MQTT_BROKER, MQTT_PORT);
  if (s_mqtt.connect(clientId.c_str(), nullptr, nullptr,
                     MQTT_TOPIC_STATUS, 0, true, "{\"online\":false}")) {
    APP_PRINTLN("MQTT: connected.");
    s_mqtt.subscribe(MQTT_TOPIC_CMD);
    s_mqtt.publish(MQTT_TOPIC_STATUS, "{\"online\":true}", true);
  } else {
    APP_PRINTF("MQTT: failed, rc=%d\n", s_mqtt.state());
  }
}

// ── Inference publish ────────────────────────────────────────────────────────

#if EI_LIB_AVAILABLE
static void publish_inference(const ei_impulse_result_t &result) {
  if (!s_mqtt.connected()) {
    return;
  }

  String p;
  p.reserve(1024);
  p  = "{\"device\":\"esp32s3\",\"ts\":";
  p += millis();
  p += ",\"model\":{\"w\":";
  p += kModelWidth;
  p += ",\"h\":";
  p += kModelHeight;
  p += "}";
  p += ",\"labels\":[";

  bool any_valid_label = false;

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  float best_value = 0.0f;
  const char *best_label = nullptr;
  uint32_t best_x = 0;
  uint32_t best_y = 0;
  uint32_t best_w = 0;
  uint32_t best_h = 0;

  for (size_t i = 0; i < result.bounding_boxes_count; i++) {
    const auto &bb = result.bounding_boxes[i];
    if (bb.value == 0.0f) {
      continue;
    }
    if (any_valid_label) {
      p += ",";
    }
    p += "{\"l\":\"";
    p += (bb.label != nullptr ? bb.label : "?");
    p += "\",\"v\":";
    p += String(bb.value, 4);
    p += ",\"x\":";
    p += bb.x;
    p += ",\"y\":";
    p += bb.y;
    p += ",\"w\":";
    p += bb.width;
    p += ",\"h\":";
    p += bb.height;
    p += "}";
    any_valid_label = true;

    if (bb.value > best_value) {
      best_value = bb.value;
      best_label = bb.label;
      best_x = bb.x;
      best_y = bb.y;
      best_w = bb.width;
      best_h = bb.height;
    }
  }
#endif

  if (!any_valid_label) {
    p += "]";
    p += ",\"top\":{\"label\":null,\"value\":0,\"x\":0,\"y\":0,\"w\":0,\"h\":0}";
  } else {
    p += "]";
    p += ",\"top\":{";
    p += "\"label\":";
    append_json_escaped(p, best_label);
    p += ",\"value\":";
    p += String(best_value, 4);
    p += ",\"x\":";
    p += best_x;
    p += ",\"y\":";
    p += best_y;
    p += ",\"w\":";
    p += best_w;
    p += ",\"h\":";
    p += best_h;
    p += "}";
  }

  p += ",\"timing\":{\"dsp\":";
  p += result.timing.dsp;
  p += ",\"inf\":";
  p += result.timing.classification;
  p += ",\"anom\":";
  p += result.timing.anomaly;
  p += "}}";

  if (!s_mqtt.publish(MQTT_TOPIC_INF, p.c_str())) {
    APP_PRINTLN("MQTT: publish failed (payload too large?).");
  }
}
#endif

static void process_command_line(const String &lineRaw) {
  String line = lineRaw;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  String upper = line;
  upper.toUpperCase();

  if (upper == "HELP") {
    APP_PRINTLN("CMD: HELP | STATUS | PING | STREAM ON | STREAM OFF | INTERVAL <100..10000>");
    return;
  }

  if (upper == "PING") {
    APP_PRINTLN("PONG");
    return;
  }

  if (upper == "STATUS") {
    APP_PRINTF("STATUS: stream=%s interval_ms=%lu model=%dx%d\n",
               s_streamEnabled ? "ON" : "OFF",
               static_cast<unsigned long>(s_inferenceIntervalMs),
               kModelWidth,
               kModelHeight);
    return;
  }

  if (upper == "STREAM ON" || upper == "START") {
    s_streamEnabled = true;
    APP_PRINTLN("STATUS: stream=ON");
    return;
  }

  if (upper == "STREAM OFF" || upper == "STOP") {
    s_streamEnabled = false;
    APP_PRINTLN("STATUS: stream=OFF");
    return;
  }

  if (upper.startsWith("INTERVAL ")) {
    int value = line.substring(9).toInt();
    if (value < 100 || value > 10000) {
      APP_PRINTLN("ERR: INTERVAL must be in range 100..10000 ms");
      return;
    }
    s_inferenceIntervalMs = static_cast<uint32_t>(value);
    APP_PRINTF("STATUS: interval_ms=%lu\n", static_cast<unsigned long>(s_inferenceIntervalMs));
    return;
  }

  APP_PRINTF("ERR: unknown command '%s'\n", line.c_str());
}

static void poll_commands(Stream &port) {
  while (port.available() > 0) {
    char c = static_cast<char>(port.read());
    if (c == '\r' || c == '\n') {
      if (s_serialCmdBuffer.length() > 0) {
        process_command_line(s_serialCmdBuffer);
        s_serialCmdBuffer = "";
      }
      continue;
    }

    if (s_serialCmdBuffer.length() < 120) {
      s_serialCmdBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1500);
  APP_PRINTLN("APP: booting firmware...");

  if (!camera_pins_are_configured()) {
    APP_PRINTLN("Camera pin macros are not configured.");
    APP_PRINTLN("Set CAMERA_PIN_* in platformio.ini build_flags using your board pinout.");
    while (true) {
      delay(1000);
    }
  }

  s_resizedRgb888 = static_cast<uint8_t *>(malloc(kModelWidth * kModelHeight * 3));
  if (!s_resizedRgb888) {
    APP_PRINTLN("Failed to allocate resized frame buffer.");
    while (true) {
      delay(1000);
    }
  }

  if (!init_camera()) {
    while (true) {
      delay(1000);
    }
  }
  APP_PRINTLN("APP: camera initialized.");

#if EI_LIB_AVAILABLE
  APP_PRINTLN("Edge Impulse library detected. Inference enabled.");
#else
  APP_PRINTLN("Edge Impulse library not found yet.");
  APP_PRINTLN("Import your Edge Impulse Arduino ZIP into lib/ and rebuild.");
#endif

  wifi_connect();
  s_mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  s_mqtt.setCallback(mqtt_on_cmd);
  mqtt_reconnect();
}

void loop() {
  mqtt_reconnect();
  s_mqtt.loop();

  poll_commands(Serial);
  poll_commands(Serial0);

  uint32_t now = millis();
  if (now - s_lastHeartbeatMs >= 5000) {
    s_lastHeartbeatMs = now;
    APP_PRINTF("APP: alive, uptime=%lu ms\n", static_cast<unsigned long>(now));
    if (s_mqtt.connected()) {
      String hb = "{\"uptime_ms\":" + String(now) + "}";
      s_mqtt.publish(MQTT_TOPIC_STATUS, hb.c_str());
    }
  }

  if (!s_streamEnabled) {
    delay(25);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    APP_PRINTLN("Frame capture failed.");
    delay(100);
    return;
  }

  if (fb->format != PIXFORMAT_RGB565) {
    APP_PRINTF("Unexpected frame format: %d\n", static_cast<int>(fb->format));
    esp_camera_fb_return(fb);
    delay(100);
    return;
  }

  resize_center_crop_rgb565_to_rgb888(fb->buf, fb->width, fb->height,
                                      s_resizedRgb888, kModelWidth, kModelHeight);
  esp_camera_fb_return(fb);

#if EI_LIB_AVAILABLE
  signal_t signal;
  signal.total_length = static_cast<size_t>(kModelWidth * kModelHeight);
  signal.get_data = &ei_camera_get_data;

  ei_impulse_result_t result = {};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    APP_PRINTF("run_classifier error: %d\n", static_cast<int>(err));
    delay(500);
    return;
  }

  APP_PRINTF("Timing: DSP=%d ms, Inference=%d ms, Anomaly=%d ms\n",
             result.timing.dsp,
             result.timing.classification,
             result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  for (size_t i = 0; i < result.bounding_boxes_count; i++) {
    const auto &bb = result.bounding_boxes[i];
    if (bb.value == 0.0f) {
      continue;
    }
    APP_PRINTF("BB %u: %s (%.5f) x=%u y=%u w=%u h=%u\n",
               static_cast<unsigned>(i),
               (bb.label != nullptr) ? bb.label : "(null-label)",
               bb.value,
               bb.x,
               bb.y,
               bb.width,
               bb.height);
  }
#else
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    const char *label = result.classification[ix].label;
    if (label == nullptr) {
      label = "(null-label)";
    }
    APP_PRINTF("%s: %.5f\n", label, result.classification[ix].value);
  }
#endif

  publish_inference(result);
  APP_PRINTLN("---");
#else
  APP_PRINTLN("Frame captured and resized. Waiting for Edge Impulse library...");
#endif

  delay(s_inferenceIntervalMs);
}