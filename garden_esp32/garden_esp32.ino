#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoMqttClient.h>
#include <DHT.h>
#include <math.h>
#include <string.h>
#include "secrets.h"

// ==================================================
// 1. CONFIGURACION DE PINES
// ==================================================

constexpr int PIN_SOIL = 32;
constexpr int PIN_RAIN = 33;
constexpr int PIN_LDR = 34;

constexpr int PIN_DHT = 27;

constexpr int PIN_TRIG = 17;
constexpr int PIN_ECHO = 5;

constexpr int PIN_LED_WET = 18;
constexpr int PIN_LED_DRY = 12;

constexpr int PIN_BUZZER = 19;
constexpr int PIN_PUMP = 23;

// ==================================================
// 2. UMBRALES DE SENSORES
// ==================================================

// Humedad del suelo
constexpr int SOIL_WET_THRESHOLD = 2000;
constexpr int SOIL_DRY_THRESHOLD = 2100;

// Lluvia y luminosidad
constexpr int RAIN_THRESHOLD = 2000;
constexpr int DARK_THRESHOLD = 1200;

// Tanque lleno observado alrededor de 4 cm. Margen de recuperacion hasta 6 cm.
// Distancia desde el sensor hasta el agua: bloqueo a 8 cm.
constexpr float TANK_LOW_DISTANCE_CM = 8.0f;
constexpr float TANK_RECOVER_DISTANCE_CM = 6.0f;

// HC-SR04
constexpr float ULTRASONIC_MIN_CM = 2.0f;
constexpr float ULTRASONIC_MAX_CM = 15.0f;

// ==================================================
// 3. INTERVALOS
// ==================================================

constexpr unsigned long SAMPLE_INTERVAL_MS = 2000;
constexpr unsigned long PUBLISH_INTERVAL_MS = 2000;
constexpr unsigned long CONNECT_INTERVAL_MS = 5000;

// ==================================================
// 4. BOMBA
// ==================================================

// Se conserva la logica HIGH = encendida.
// Verifica que coincida con tu modulo de control.

constexpr int PUMP_ON_LEVEL = HIGH;
constexpr int PUMP_OFF_LEVEL = LOW;

// ==================================================
// 5. OBJETOS Y VARIABLES
// ==================================================

DHT dht(PIN_DHT, DHT11);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

uint32_t bootId;
uint32_t sequenceNumber = 0;

unsigned long lastSample = 0;
unsigned long lastConnectAttempt = 0;
unsigned long lastPublished = 0;

bool hasPublished = false;
bool forcePublish = true;

// Estados de sensores

bool soilWet = false;
bool raining = false;
bool dark = false;

bool tankLow = true;
bool tankLevelValid = false;

// Estados de actuadores

bool pumpOn = false;

// Estados anteriores

bool previousSoilWet = false;
bool previousRaining = false;
bool previousTankLow = true;
bool previousDark = false;
bool previousTankValid = false;
bool previousPumpOn = false;

char topic[80];
bool mdnsStarted = false;

// ==================================================
// 6. LECTURA ANALOGICA CON PROMEDIO
// ==================================================

int readAnalogAverage(int pin) {

  long total = 0;

  for (int i = 0; i < 5; i++) {

    total += analogRead(pin);

    delayMicroseconds(500);
  }

  return total / 5;
}

// ==================================================
// 7. SENSOR DE HUMEDAD DEL SUELO
// ==================================================

void updateSoilState(int soil) {

  if (soil <= SOIL_WET_THRESHOLD) {

    soilWet = true;

  } else if (soil >= SOIL_DRY_THRESHOLD) {

    soilWet = false;
  }

  // Entre 2000 y 2100 conserva
  // el estado anterior para evitar fluctuaciones.
}

// ==================================================
// 8. SENSOR ULTRASONICO HC-SR04
// ==================================================

float measureDistanceCm() {

  float readings[3];

  int count = 0;

  for (int i = 0; i < 3; i++) {

    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);

    digitalWrite(PIN_TRIG, LOW);

    unsigned long duration =
      pulseIn(PIN_ECHO, HIGH, 30000UL);

    if (duration > 0) {

      float distance =
        duration * 0.0343f / 2.0f;

      if (distance >= ULTRASONIC_MIN_CM &&
          distance <= ULTRASONIC_MAX_CM) {

        readings[count++] = distance;
      }
    }

    delay(40);
  }

  if (count == 0) {

    return -1.0f;
  }

  // Ordenar lecturas.

  for (int i = 0; i < count - 1; i++) {

    for (int j = i + 1; j < count; j++) {

      if (readings[i] > readings[j]) {

        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }

  // Obtener mediana.

  if (count == 1) {
    return readings[0];
  }

  if (count == 2) {
    return (readings[0] + readings[1]) / 2.0f;
  }

  return readings[1];
}

// ==================================================
// 9. CONTROL DEL NIVEL DEL TANQUE
// ==================================================

void updateTankLevel(float distance) {

  // Si falla el sensor, bloquear la bomba.

  if (distance < ULTRASONIC_MIN_CM ||
      distance > ULTRASONIC_MAX_CM) {

    tankLevelValid = false;
    tankLow = true;

    return;
  }

  tankLevelValid = true;

  // Distancia grande = menos agua.

  if (distance >= TANK_LOW_DISTANCE_CM) {

    tankLow = true;
  }

  // Distancia pequena = mas agua.

  else if (distance <= TANK_RECOVER_DISTANCE_CM) {

    tankLow = false;
  }

  // Entre 6 y 8 cm mantiene el estado anterior.
}

// ==================================================
// 10. CONTROL DE DOS LED Y BUZZER
// ==================================================

void updateIndicators() {

  // GPIO 18: suelo humedo.

  digitalWrite(
    PIN_LED_WET,
    soilWet ? HIGH : LOW
  );

  // GPIO 12: suelo seco.

  digitalWrite(
    PIN_LED_DRY,
    soilWet ? LOW : HIGH
  );

  // Buzzer: lluvia.

  digitalWrite(
    PIN_BUZZER,
    raining ? HIGH : LOW
  );
}

// ==================================================
// 11. CONTROL AUTOMATICO DE LA BOMBA
// ==================================================

void updatePump() {

  // Verificar disponibilidad de agua.

  bool waterAvailable =
    tankLevelValid && !tankLow;

  // Verificar necesidad de riego.

  bool irrigationRequired =
    !soilWet &&
    waterAvailable &&
    !raining;

  pumpOn = irrigationRequired;

  // Aplicar control.

  digitalWrite(
    PIN_PUMP,
    pumpOn ? PUMP_ON_LEVEL : PUMP_OFF_LEVEL
  );
}

// ==================================================
// 12. CONEXION WIFI Y MQTT
// ==================================================

void connectNetwork() {
  const unsigned long now = millis();
  if (now - lastConnectAttempt < CONNECT_INTERVAL_MS) return;
  lastConnectAttempt = now;
  if (WiFi.status() != WL_CONNECTED) {
    if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
    Serial.print("Wi-Fi: esperando conexion a ");
    Serial.print(WIFI_SSID);
    Serial.print(" (estado ");
    Serial.print(WiFi.status());
    Serial.println(')');
    return;
  }
  if (!mqttClient.connected()) {
    // Mantener la bomba apagada durante intentos de red que puedan bloquear.
    pumpOn = false;
    digitalWrite(PIN_PUMP, PUMP_OFF_LEVEL);
    String brokerHost = MQTT_HOST;
    int brokerPort = MQTT_PORT;
    if (brokerHost.length() == 0) {
      if (!mdnsStarted) mdnsStarted = MDNS.begin(DEVICE_ID);
      const int count = mdnsStarted ? MDNS.queryService("jardin", "tcp") : 0;
      for (int i = 0; i < count; i++) {
        if (MDNS.txt(i, "device") == DEVICE_ID) {
          const IPAddress candidate = MDNS.address(i);
          const IPAddress local = WiFi.localIP();
          const IPAddress mask = WiFi.subnetMask();
          if (candidate == IPAddress(0, 0, 0, 0)) continue;
          bool sameNetwork = true;
          for (int octet = 0; octet < 4; octet++) {
            if ((candidate[octet] & mask[octet]) != (local[octet] & mask[octet])) sameNetwork = false;
          }
          if (!sameNetwork) continue;
          brokerHost = candidate.toString();
          brokerPort = MDNS.port(i);
          break;
        }
      }
      if (brokerHost.length() == 0) {
        Serial.println("Jardin no encontrado: abre jardin.exe en la laptop; revisa Wi-Fi y firewall.");
        return;
      }
    }
    Serial.print("Wi-Fi conectado. IP ESP32: ");
    Serial.println(WiFi.localIP());
    Serial.print("MQTT: conectando a ");
    Serial.print(brokerHost);
    Serial.print(':');
    Serial.println(brokerPort);
    mqttClient.setId(DEVICE_ID);
    mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
    if (mqttClient.connect(brokerHost.c_str(), brokerPort)) {
      forcePublish = true;
      Serial.println("MQTT: conectado");
    } else {
      Serial.print("MQTT: fallo de conexion. Error: ");
      Serial.println(mqttClient.connectError());
    }
  }
}

// ==================================================
// 13. PUBLICACION MQTT
// ==================================================

void publishTelemetry(
  unsigned long now,
  int soil,
  int rain,
  int light,
  float temperature,
  float humidity,
  float distance
) {

  if (!mqttClient.connected()) {
    return;
  }

  // Detectar cambios importantes.

  const bool changed =
    soilWet != previousSoilWet ||
    raining != previousRaining ||
    tankLow != previousTankLow ||
    dark != previousDark ||
    tankLevelValid != previousTankValid ||
    pumpOn != previousPumpOn;

  // Publicar si hay cambios o pasan 2 segundos.

  if (!forcePublish &&
      hasPublished &&
      !changed &&
      now - lastPublished < PUBLISH_INTERVAL_MS) {

    return;
  }

  char tempValue[16];
  char humidityValue[16];
  char distanceValue[16];

  // Temperatura.

  if (isnan(temperature)) {

    strcpy(tempValue, "null");

  } else {

    snprintf(
      tempValue,
      sizeof(tempValue),
      "%.1f",
      temperature
    );
  }

  // Humedad ambiental.

  if (isnan(humidity)) {

    strcpy(humidityValue, "null");

  } else {

    snprintf(
      humidityValue,
      sizeof(humidityValue),
      "%.1f",
      humidity
    );
  }

  // Distancia.

  if (!tankLevelValid) {

    strcpy(distanceValue, "null");

  } else {

    snprintf(
      distanceValue,
      sizeof(distanceValue),
      "%.1f",
      distance
    );
  }

  // Construir JSON.

  char payload[400];

  const int length = snprintf(
    payload,
    sizeof(payload),

    "{\"v\":1,"
    "\"id\":\"%s\","
    "\"b\":%lu,"
    "\"q\":%lu,"
    "\"s\":%d,"
    "\"r\":%d,"
    "\"l\":%d,"
    "\"t\":%s,"
    "\"h\":%s,"
    "\"d\":%s,"
    "\"sw\":%d,"
    "\"rw\":%d,"
    "\"tl\":%d,"
    "\"dk\":%d,"
    "\"tv\":%d,"
    "\"p\":%d,"
    "\"lw\":%d,"
    "\"ld\":%d,"
    "\"bz\":%d}",

    DEVICE_ID,

    (unsigned long)bootId,
    (unsigned long)sequenceNumber,

    soil,
    rain,
    light,

    tempValue,
    humidityValue,
    distanceValue,

    soilWet,
    raining,
    tankLow,
    dark,

    tankLevelValid,

    pumpOn,

    soilWet,
    !soilWet,
    raining
  );

  if (length <= 0 ||
      length >= (int)sizeof(payload)) {

    Serial.println("MQTT: error construyendo JSON");

    return;
  }

  // Publicacion con QoS 1.

  if (mqttClient.beginMessage(
        topic,
        (unsigned long)length,
        false,
        1
      )) {

    mqttClient.write(
      (const uint8_t*)payload,
      length
    );

    if (mqttClient.endMessage()) {

      Serial.print("MQTT enviado: ");
      Serial.println(payload);

      sequenceNumber++;

      lastPublished = now;
      hasPublished = true;
      forcePublish = false;

      // Guardar estados anteriores.

      previousSoilWet = soilWet;
      previousRaining = raining;
      previousTankLow = tankLow;
      previousDark = dark;
      previousTankValid = tankLevelValid;
      previousPumpOn = pumpOn;

    } else {

      Serial.println("MQTT: error al terminar envio");
    }

  } else {

    Serial.println("MQTT: error al iniciar envio");
  }
}

// ==================================================
// 14. CONFIGURACION INICIAL
// ==================================================

void setup() {

  Serial.begin(115200);

  analogReadResolution(12);

  // Sensores.

  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_RAIN, INPUT);
  pinMode(PIN_LDR, INPUT);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // LED.

  pinMode(PIN_LED_WET, OUTPUT);
  pinMode(PIN_LED_DRY, OUTPUT);

  // Actuadores.

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);

  // Inicializar salidas apagadas.

  digitalWrite(PIN_TRIG, LOW);

  digitalWrite(PIN_LED_WET, LOW);
  digitalWrite(PIN_LED_DRY, LOW);

  digitalWrite(PIN_BUZZER, LOW);

  digitalWrite(PIN_PUMP, PUMP_OFF_LEVEL);

  // Iniciar DHT11.

  dht.begin();

  bootId = esp_random();

  // Mantener el topic MQTT original.

  snprintf(
    topic,
    sizeof(topic),
    "garden/%s/telemetry",
    DEVICE_ID
  );

  // Iniciar Wi-Fi.

  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);
  mqttClient.setConnectionTimeout(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("        SMART GARDEN IoT");
  Serial.println("        ESP32 INICIANDO");
  Serial.println("================================");

  Serial.print("Wi-Fi: ");
  Serial.println(WIFI_SSID);

  Serial.print("MQTT Topic: ");
  Serial.println(topic);

  Serial.println("LED HUMEDO: GPIO 18");
  Serial.println("LED SECO: GPIO 12");

  Serial.println("ULTRASONICO TRIG: GPIO 17");
  Serial.println("ULTRASONICO ECHO: GPIO 5");

  Serial.println("BOMBA: GPIO 23");

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );
}

// ==================================================
// 15. BUCLE PRINCIPAL
// ==================================================

void loop() {

  connectNetwork();

  if (mqttClient.connected()) {
    mqttClient.poll();
  }

  const unsigned long now = millis();

  // Muestreo cada dos segundos.

  if (now - lastSample < SAMPLE_INTERVAL_MS) {
    return;
  }

  lastSample = now;

  // -----------------------------------------------
  // LECTURA DE SENSORES
  // -----------------------------------------------

  const int soil = readAnalogAverage(PIN_SOIL);
  const int rain = readAnalogAverage(PIN_RAIN);
  const int light = readAnalogAverage(PIN_LDR);

  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();

  const float distance = measureDistanceCm();

  // -----------------------------------------------
  // PROCESAR HUMEDAD DEL SUELO
  // -----------------------------------------------

  updateSoilState(soil);

  // -----------------------------------------------
  // PROCESAR LLUVIA Y LUZ
  // -----------------------------------------------

  raining = rain < RAIN_THRESHOLD;

  dark = light < DARK_THRESHOLD;

  // -----------------------------------------------
  // PROCESAR NIVEL DEL TANQUE
  // -----------------------------------------------

  updateTankLevel(distance);

  // -----------------------------------------------
  // ACTUALIZAR LED Y BUZZER
  // -----------------------------------------------

  updateIndicators();

  // -----------------------------------------------
  // CONTROLAR BOMBA
  // -----------------------------------------------

  updatePump();

  // -----------------------------------------------
  // MONITOR SERIAL
  // -----------------------------------------------

  Serial.println();

  Serial.println("========== SMART GARDEN ==========");

  Serial.printf(
    "Suelo: %d (%s)\n",
    soil,
    soilWet ? "HUMEDO" : "SECO"
  );

  Serial.printf(
    "Lluvia: %d (%s)\n",
    rain,
    raining ? "LLUVIA" : "SIN LLUVIA"
  );

  Serial.printf(
    "Luz: %d (%s)\n",
    light,
    dark ? "OSCURIDAD" : "ILUMINADO"
  );

  if (isnan(temperature)) {
    Serial.println("Temperatura: ERROR DHT11");
  } else {
    Serial.printf("Temperatura: %.1f C\n", temperature);
  }

  if (isnan(humidity)) {
    Serial.println("Humedad aire: ERROR DHT11");
  } else {
    Serial.printf("Humedad aire: %.1f %%\n", humidity);
  }

  if (tankLevelValid) {

    Serial.printf(
      "Distancia tanque: %.1f cm\n",
      distance
    );

    Serial.printf(
      "Estado tanque: %s\n",
      tankLow ? "NIVEL BAJO" : "AGUA DISPONIBLE"
    );

  } else {

    Serial.println("Tanque: ERROR DE MEDICION");
    Serial.println("BOMBA BLOQUEADA POR SEGURIDAD");
  }

  Serial.printf(
    "LED GPIO 18: %s\n",
    soilWet ? "ENCENDIDO" : "APAGADO"
  );

  Serial.printf(
    "LED GPIO 12: %s\n",
    soilWet ? "APAGADO" : "ENCENDIDO"
  );

  Serial.printf(
    "Buzzer: %s\n",
    raining ? "ENCENDIDO" : "APAGADO"
  );

  Serial.printf(
    "Bomba: %s\n",
    pumpOn ? "ENCENDIDA" : "APAGADA"
  );

  Serial.println("==================================");

  // -----------------------------------------------
  // ENVIAR DATOS AL DASHBOARD
  // -----------------------------------------------

  publishTelemetry(
    now,
    soil,
    rain,
    light,
    temperature,
    humidity,
    distance
  );
}
