/*
=================================================================================
  SISTEMA COMPLETO DE TELEMETRIA, APOGEU E DATALOGGER — ESP32 + LORA E22
=================================================================================
  Periféricos e Pinos:
    - Cartão SD (SPI):    CS = GPIO 5 (MOSI=23, MISO=19, SCK=18)
    - GPS (UART Serial2): TX GPS -> GPIO D16 (RX2), RX GPS -> GPIO D17 (TX2)
    - BMP280 (I2C 0x76):  SDA = GPIO D21, SCL = GPIO D22
    - BNO055 (I2C 0x29):  SDA = GPIO D21, SCL = GPIO D22
    - Squib / SKIB:       MOSFET -> GPIO D12
    - LoRa E22 (UART1):   RXD -> GPIO D32 (TX1), TXD -> GPIO D33 (RX1), AUX -> GPIO D25
                          M0 -> GND, M1 -> GND
=================================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <LoRa_E22.h> // Biblioteca ajustada para E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

// --- PINOS ---
const int chipSelect = 5;          // CS do Cartão SD (SPI)
#define GPS_RX_PIN        16       // Recebe do TX do GPS (RX2)
#define GPS_TX_PIN        17       // Envia para o RX do GPS (TX2)
#define SDA_PIN           21       // I2C SDA
#define SCL_PIN           22       // I2C SCL
#define PINO_SQUIB        12       // Gate do MOSFET (Acionador Apogeu)

#define LORA_RX_PIN       33       // RX ESP32 (Conectado ao TXD do E22)
#define LORA_TX_PIN       32       // TX ESP32 (Conectado ao RXD do E22)
#define PIN_E22_AUX       25       // AUX do E22

// --- PARÂMETROS DE APOGEU ---
#define ALTITUDE_ALVO     0.4      // Altitude mínima (m) para liberar disparo
#define QUEDA_MARGEM      1.5      // Queda (m) a partir do pico para confirmar apogeu
#define TEMPO_PULSO_SQUIB 2000     // Duração do pulso em ms (2 segundos)

// --- AMOSTRAGEM ---
#define SAMPLE_RATE_MS    100      // Frequência de 10 Hz para Log no SD
#define SENSOR_ID_BNO     55

// --- OBJETOS ---
TinyGPSPlus gps;
#define gpsSerial Serial2

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED); // Objeto E22

Adafruit_BMP280 altimetro;
Adafruit_BNO055 bno = Adafruit_BNO055(SENSOR_ID_BNO, 0x29, &Wire);

// --- ESTRUTURA DE TELEMETRIA LORA (53 BYTES PACKED) ---
struct __attribute__((packed)) TelemetriaPacket {
  uint32_t tempoMs;
  uint32_t idMensagem;
  float latitude;
  float longitude;
  float altitudeGPS;
  uint8_t satelites;
  float altitudeBMP;
  float maxAltitude;
  float pressao;
  float accX, accY, accZ;
  float quatW, quatX, quatY, quatZ;
  uint8_t statusSquib; // 0: AGUARDANDO, 1: DISPARANDO, 2: DISPARADO
};

// --- VARIÁVEIS DE CONTROLE ---
float pressaoBase = 0.0;
float maxAltitudeAtingida = 0.0;
bool acionado = false;
bool squibAtivo = false;
unsigned long tempoInicioAcionamento = 0;
unsigned long ultimoCiclo = 0;
uint32_t contadorPacotesLoRa = 0;

void setup() {
  // 1. Pino de segurança do acionador
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO AVIONICA INTEGRADA (SD + GPS + BMP280 + BNO055 + LORA E22) ===");

  // 2. GPS (UART2)
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // 3. LoRa E22 (UART1)
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();
  Serial.println("LoRa E22 Inicializado!");

  // 4. Cartão SD (SPI)
  if (!SD.begin(chipSelect)) {
    Serial.println("ERRO CRÍTICO: Cartão SD não inicializado!");
  } else {
    Serial.println("Cartão SD OK!");
    if (!SD.exists("/voo_telemetria.csv")) {
      File logFile = SD.open("/voo_telemetria.csv", FILE_WRITE);
      if (logFile) {
        logFile.println("Tempo_ms,Data,Hora_UTC,Lat,Lng,Sat,Alt_GPS_m,Vel_kmh,Alt_BMP_m,Alt_Max_m,Pressao_hPa,Status_Squib,Acc_X,Acc_Y,Acc_Z,Quat_W,Quat_X,Quat_Y,Quat_Z");
        logFile.close();
        Serial.println("Arquivo voo_telemetria.csv criado!");
      }
    }
  }

  // 5. Barramento I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // BMP280
  if (!altimetro.begin(0x76)) {
    Serial.println("ERRO CRÍTICO: BMP280 não encontrado (0x76)!");
    while (1) delay(10);
  }
  altimetro.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16,
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_63);

  // Calibração de pressão base no solo
  float somaPressao = 0;
  for (int i = 0; i < 10; i++) {
    somaPressao += altimetro.readPressure();
    delay(50);
  }
  pressaoBase = (somaPressao / 10.0) / 100.0;
  Serial.printf("BMP280 OK! Pressão Base: %.2f hPa\n", pressaoBase);

  // BNO055
  if (!bno.begin()) {
    Serial.println("ERRO CRÍTICO: BNO055 não encontrado (0x29)!");
    while (1) delay(10);
  }
  delay(500);
  bno.setExtCrystalUse(true);
  Serial.println("BNO055 OK!");

  Serial.println("=========================================================\n");
}

void loop() {
  // 1. Decodificação contínua do GPS
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // 2. Loop de amostragem (10 Hz)
  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimoCiclo >= SAMPLE_RATE_MS) {
    ultimoCiclo = tempoAtual;
    executarTelemetria(tempoAtual);
  }
}

void executarTelemetria(unsigned long tempoAtual) {
  // --- A. LEITURA BMP280 & LÓGICA DE APOGEU ---
  float pressao = altimetro.readPressure() / 100.0;
  float altitudeAtual = altimetro.readAltitude(pressaoBase);

  if (altitudeAtual > maxAltitudeAtingida) {
    maxAltitudeAtingida = altitudeAtual;
  }

  bool apogeuDetectado = (maxAltitudeAtingida >= ALTITUDE_ALVO) && 
                         (altitudeAtual <= (maxAltitudeAtingida - QUEDA_MARGEM));

  if (apogeuDetectado && !acionado) {
    acionado = true;
    squibAtivo = true;
    tempoInicioAcionamento = tempoAtual;
    digitalWrite(PINO_SQUIB, HIGH);
    Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!! APOGEU DETECTADO - DISPARANDO SQUIB / SKIB !!!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  }

  if (squibAtivo && (tempoAtual - tempoInicioAcionamento >= TEMPO_PULSO_SQUIB)) {
    digitalWrite(PINO_SQUIB, LOW);
    squibAtivo = false;
    Serial.println(">>> Pulso do Squib encerrado por segurança.");
  }

  uint8_t squibCode = 0; // AGUARDANDO
  String statusSquibStr = "AGUARDANDO";
  if (squibAtivo) {
    squibCode = 1; // DISPARANDO
    statusSquibStr = "DISPARANDO";
  } else if (acionado) {
    squibCode = 2; // DISPARADO
    statusSquibStr = "DISPARADO";
  }

  // --- B. LEITURA BNO055 ---
  sensors_event_t accelData;
  bno.getEvent(&accelData, Adafruit_BNO055::VECTOR_LINEARACCEL);
  imu::Quaternion quat = bno.getQuat();

  // --- C. TRATAMENTO DE DADOS DO GPS ---
  String dataStr = gps.date.isValid() ? String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) : "N/A";
  String horaStr = gps.time.isValid() ? String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) : "N/A";
  float latVal   = gps.location.isValid() ? gps.location.lat() : 0.0;
  float lngVal   = gps.location.isValid() ? gps.location.lng() : 0.0;
  float altGps   = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  float velGps   = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  uint8_t sats   = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // --- D. GRAVAÇÃO NO SD CARD ---
  File logFile = SD.open("/voo_telemetria.csv", FILE_APPEND);
  if (logFile) {
    logFile.printf("%lu,%s,%s,%.6f,%.6f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                   tempoAtual, dataStr.c_str(), horaStr.c_str(),
                   latVal, lngVal, sats, altGps, velGps,
                   altitudeAtual, maxAltitudeAtingida, pressao,
                   statusSquibStr.c_str(),
                   accelData.acceleration.x, accelData.acceleration.y, accelData.acceleration.z,
                   quat.w(), quat.x(), quat.y(), quat.z());
    logFile.close();
  }

  // --- E. TRANSMISSÃO LORA E22 ---
  TelemetriaPacket pkt;
  pkt.tempoMs     = tempoAtual;
  pkt.idMensagem  = contadorPacotesLoRa++;
  pkt.latitude    = latVal;
  pkt.longitude   = lngVal;
  pkt.altitudeGPS = altGps;
  pkt.satelites   = sats;
  pkt.altitudeBMP = altitudeAtual;
  pkt.maxAltitude = maxAltitudeAtingida;
  pkt.pressao     = pressao;
  pkt.accX        = accelData.acceleration.x;
  pkt.accY        = accelData.acceleration.y;
  pkt.accZ        = accelData.acceleration.z;
  pkt.quatW       = quat.w();
  pkt.quatX       = quat.x();
  pkt.quatY       = quat.y();
  pkt.quatZ       = quat.z();
  pkt.statusSquib = squibCode;

  // Envio usando a biblioteca LoRa_E22
  ResponseStatus rs = e22.sendMessage(&pkt, sizeof(TelemetriaPacket));

  // --- F. MONITOR SERIAL COMPACTO ---
  Serial.printf("[TX E22 #%u | %lums] BMP: %.1fm | Max: %.1fm | Sat: %d | Squib: %s | LoRa: %s\n",
                pkt.idMensagem, tempoAtual, altitudeAtual, maxAltitudeAtingida, sats, statusSquibStr.c_str(), rs.getResponseDescription().c_str());
}