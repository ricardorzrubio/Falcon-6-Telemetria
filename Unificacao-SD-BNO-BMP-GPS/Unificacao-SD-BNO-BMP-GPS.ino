/*
=================================================================================
  SISTEMA COMPLETO DE TELEMETRIA, APOGEU E DATALOGGER — ESP32 (30 PINOS)
=================================================================================
  Periféricos e Pinos:
    - Cartão SD (SPI):     CS = GPIO 5 (MOSI=23, MISO=19, SCK=18)
    - GPS (UART Serial2): TX GPS -> GPIO D17 (RX), RX GPS -> GPIO D16 (TX)
    - BMP280 (I2C 0x76):  SDA = GPIO D21, SCL = GPIO D22
    - BNO055 (I2C 0x29):  SDA = GPIO D21, SCL = GPIO D22
    - Squib / SKIB:      MOSFET -> GPIO D13
=================================================================================
*/

// SD
#include <SPI.h>
#include <SD.h>

// GPS NEO 6M
#include <TinyGPSPlus.h>

// BMP280 e BNO055
#include <Wire.h>
#include <Adafruit_Sensor.h>

// BMP280
#include <Adafruit_BMP280.h>

// BNO055
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

// SD
const int chipSelect = 5;          // CS do Cartão SD (SPI)

// GPS NEO 6M
#define GPS_RX_PIN        16       // Recebe do TX do GPS
#define GPS_TX_PIN        17       // Envia para o RX do GPS
TinyGPSPlus gps;
#define gpsSerial Serial2

// BMP280 e BNO055
#define SDA_PIN           21       // I2C SDA
#define SCL_PIN           22       // I2C SCL

// BMP280
#define PINO_SQUIB        13       // Gate do MOSFET (Acionador Apogeu)
#define ALTITUDE_ALVO     0.4      // Altitude mínima (m) para liberar disparo
#define QUEDA_MARGEM      0.1      // Queda (m) a partir do pico para confirmar apogeu
#define TEMPO_PULSO_SQUIB 2000     // Duração do pulso em ms (2 segundos)
Adafruit_BMP280 altimetro;

// BNO055
#define SAMPLE_RATE_MS    100      // Frequência de 10 Hz (100 ms)
#define SENSOR_ID_BNO     55
Adafruit_BNO055 bno = Adafruit_BNO055(SENSOR_ID_BNO, 0x29, &Wire);

// --- VARIÁVEIS DE CONTROLE ---
float pressaoBase = 0.0; // BMP280
float maxAltitudeAtingida = 0.0; // BMP280
bool acionado = false; // BMP280
bool squibAtivo = false; // BMP280
unsigned long tempoInicioAcionamento = 0; // BMP280
unsigned long ultimoCiclo = 0;

void setup() {
  // 1. Pino de segurança do acionador
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO AVIONICA INTEGRADA (SD + GPS + BMP280 + BNO055) ===");

  // 2. GPS (UART)
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // 3. Cartão SD (SPI)
  if (!SD.begin(chipSelect)) {
    Serial.println("ERRO CRÍTICO: Cartão SD não inicializado!");
    while (1) delay(10);
  }
  Serial.println("Cartão SD OK!");

  // Cabeçalho CSV
  if (!SD.exists("/voo_telemetria.csv")) {
    File logFile = SD.open("/voo_telemetria.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("Tempo_ms,Data,Hora_UTC,Lat,Lng,Sat,Alt_GPS_m,Vel_kmh,Alt_BMP_m,Alt_Max_m,Pressao_hPa,Status_Squib,Acc_X,Acc_Y,Acc_Z,Quat_W,Quat_X,Quat_Y,Quat_Z");
      logFile.close();
      Serial.println("Arquivo voo_telemetria.csv criado!");
    }
  }

  // 4. Barramento I2C
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

  // 2. Executa leituras e gravação a cada 100 ms (10 Hz)
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

  String statusSquibStr = "AGUARDANDO";
  if (squibAtivo) statusSquibStr = "DISPARANDO";
  else if (acionado) statusSquibStr = "DISPARADO";

  // --- B. LEITURA BNO055 ---
  sensors_event_t accelData;
  bno.getEvent(&accelData, Adafruit_BNO055::VECTOR_LINEARACCEL);
  imu::Quaternion quat = bno.getQuat();

  // --- C. TRATAMENTO DE DADOS DO GPS ---
  String dataStr = gps.date.isValid() ? String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) : "N/A";
  String horaStr = gps.time.isValid() ? String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) : "N/A";
  String latStr  = gps.location.isValid() ? String(gps.location.lat(), 6) : "0.0";
  String lngStr  = gps.location.isValid() ? String(gps.location.lng(), 6) : "0.0";
  float altGps    = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  float velGps    = gps.speed.isValid() ? gps.speed.kmph() : 0.0;

  // --- D. GRAVAÇÃO NO SD CARD ---
  File logFile = SD.open("/voo_telemetria.csv", FILE_APPEND);
  if (logFile) {
    logFile.printf("%lu,%s,%s,%s,%s,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                   tempoAtual, dataStr.c_str(), horaStr.c_str(),
                   latStr.c_str(), lngStr.c_str(), gps.satellites.value(),
                   altGps, velGps,
                   altitudeAtual, maxAltitudeAtingida, pressao,
                   statusSquibStr.c_str(),
                   accelData.acceleration.x, accelData.acceleration.y, accelData.acceleration.z,
                   quat.w(), quat.x(), quat.y(), quat.z());
    logFile.close();
  } else {
    Serial.println("-> [ERRO SD] Falha ao abrir /voo_telemetria.csv");
  }

  // --- E. MONITOR SERIAL ---
  Serial.println("--------------------------------------------------");
  Serial.printf("[ALTITUDE] Atual: %.2f m | Max: %.2f m | Status SKIB: %s\n", altitudeAtual, maxAltitudeAtingida, statusSquibStr.c_str());
  Serial.printf("[GPS] Lat: %s | Lng: %s | Sat: %d | Alt: %.1f m\n", latStr.c_str(), lngStr.c_str(), gps.satellites.value(), altGps);
  Serial.printf("[ACCEL LIN] X: %.2f | Y: %.2f | Z: %.2f m/s²\n", accelData.acceleration.x, accelData.acceleration.y, accelData.acceleration.z);
  Serial.printf("[QUAT] W: %.2f | X: %.2f | Y: %.2f | Z: %.2f\n", quat.w(), quat.x(), quat.y(), quat.z());
}