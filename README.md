# Jardín inteligente · firmware ESP32

El jardín avisa cómo se siente. Este firmware lee sensores ambientales y del
suelo, controla el riego con condiciones de seguridad y publica telemetría al
backend Go por MQTT.

## Hardware

- Placa ESP32 Dev Module.
- Sensor de humedad de suelo, lluvia, luz y temperatura/humedad DHT11.
- Sensor ultrasónico para nivel de agua.
- Indicadores LED, buzzer y etapa de potencia para la bomba.

Consulta la tabla de GPIO y los umbrales al final de este README. Ajusta los
valores a tus sensores reales antes de usar el sistema.

## Preparar y cargar

1. Instala Arduino IDE y la plataforma **ESP32 by Espressif Systems 3.3.12**.
2. Instala **ArduinoMqttClient 0.1.8**, **DHT sensor library 1.4.7** y
   **Adafruit Unified Sensor 1.1.15**.
3. Copia `garden_esp32/secrets.example.h` como `garden_esp32/secrets.h`.
4. Configura Wi-Fi y credenciales MQTT en `secrets.h`.
5. Abre `garden_esp32/garden_esp32.ino`, selecciona **ESP32 Dev Module** y
   carga la placa.

`secrets.h` contiene datos privados y está excluido de Git. No compartas tu
contraseña Wi-Fi ni tus credenciales MQTT.

## Pines

| Componente | GPIO |
|---|---:|
| Humedad de suelo | 32 |
| Lluvia | 33 |
| LDR | 34 |
| DHT11 DATA | 27 |
| Ultrasonido TRIG / ECHO | 17 / 5 |
| LED suelo húmedo / suelo seco | 18 / 12 |
| Buzzer | 19 |
| Control de bomba | 23 |

El GPIO 34 usa ADC1. Protege el pin ECHO para que no reciba más de 3.3 V. La
bomba requiere una etapa de potencia y protección; nunca la conectes directo a
un GPIO.

## Riego y telemetría

El riego automático se habilita cuando el suelo está seco, hay agua disponible
y no llueve. Sin medición válida del nivel o durante una reconexión MQTT, la
bomba permanece apagada. El firmware promedia cinco muestras del suelo, filtra
tres mediciones de distancia y envía telemetría cada dos segundos.

MQTT publica en `garden/garden-01/telemetry` con QoS 1. Si `MQTT_HOST` queda
vacío, el ESP32 busca el backend en la red local mediante mDNS. La red debe
permitir comunicación entre dispositivos. MQTT local no cifra el tráfico: no
expongas el puerto a Internet.

## Repositorios relacionados

- [App Flutter](https://github.com/Jonathan03R/jardin-inteligente-flutter)
- [Backend Go](https://github.com/Jonathan03R/jardin-inteligente-backend-go)

## Proyecto universitario y licencia

Prototipo académico de Internet de las cosas. Se distribuye bajo licencia MIT;
consulta [LICENSE](LICENSE). Úsalo como base de aprendizaje y calibra sensores,
actuadores y umbrales antes de instalarlo en un jardín real.
