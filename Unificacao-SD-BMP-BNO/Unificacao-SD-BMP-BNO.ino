/*
=================================================================================
  SISTEMA INTEGRADO DE NAVEGAÇÃO, APOGEU E DATALOGGER (ESP32)
=================================================================================
  Barramento SPI:
    - Cartão SD (CS -> GPIO 5, MOSI -> 23, MISO -> 19, SCK -> 18)
  Barramento I2C:
    - BMP280 (Altímetro - Endereço 0x76, SDA -> 21, SCL -> 22)
    - BNO055 (IMU 9-DOF - Endereço 0x28, SDA -> 21, SCL -> 22)
  Atuador:
    - SKIB / Squib (MOSFET em GPIO 12)
=================================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

// --- PINOS E CONFIGURAÇÕES ---
const int chipSelect = 5;            // CS do SD Card (SPI)
#define SDA_PIN             21       // I2C SDA
#define SCL_PIN             22       // I2C SCL
#define PINO_SQUIB          12       // MOSFET Squib / SKIB

// --- CONFIGURAÇÕES DE APOGEU (BMP280) ---
#define ALTITUDE_ALVO       0.4      // Altitude mínima em metros para habilitar disparo
#define QUEDA_MARGEM        1.5      // Queda necessária (m) a partir do pico para confirmar apogeu
#define TEMPO_PULSO_SQUIB   2000     // Tempo do pulso em ms (2 segundos)

// --- TELEMETRIA E TEMPO ---
#define SAMPLE_RATE_MS      100      // Ciclo de leitura/gravação (10 Hz = 100ms)
#define SENSOR_ID_BNO       55

// Instância dos objetos
Adafruit_BMP280 altimetro;
Adafruit_BNO055 bno = Adafruit_BNO055(SENSOR_ID_BNO, 0x29, &Wire);

// Variáveis de Controle
float pressaoBase = 0.0;
float maxAltitudeAtingida = 0.0;
bool acionado = false;
bool squibAtivo = false;
unsigned long tempoInicioAcionamento = 0;

void setup() {
  // 1. Configuração do Pino de Disparo
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO SISTEMA COMPLETO (SD + BMP280 + BNO055) ===");

  // 2. Inicialização do Cartão SD (SPI)
  if (!SD.begin(chipSelect)) {
    Serial.println("ERRO CRÍTICO: Cartão SD não encontrado ou falha nas conexões!");
    while (1) delay(10);
  }
  Serial.println("Cartão SD OK!");

  // Cria o cabeçalho no CSV se o arquivo não existir
  if (!SD.exists("/telemetria.csv")) {
    File logFile = SD.open("/telemetria.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("Tempo_ms,Alt_Atual_m,Alt_Max_m,Pressao_hPa,Status_Squib,Acc_X,Acc_Y,Acc_Z,Quat_W,Quat_X,Quat_Y,Quat_Z");
      logFile.close();
      Serial.println("Arquivo telemetria.csv criado com cabeçalho!");
    }
  }

  // 3. Inicialização do Barramento I2C e Sensores
  Wire.begin(SDA_PIN, SCL_PIN);

  // BMP280
  if (!altimetro.begin(0x76)) {
    Serial.println("ERRO CRÍTICO: BMP280 não encontrado no endereço 0x76!");
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
    Serial.println("ERRO CRÍTICO: BNO055 não encontrado no endereço 0x29!");
    while (1) delay(10);
  }

  delay(500);
  bno.setExtCrystalUse(true);
  Serial.println("BNO055 OK!");
  Serial.println("=========================================================\n");
}

void loop() {
  unsigned long tempoAtual = millis();

  // -------------------------------------------------------------
  // 1. BMP280: ALTITUDE E LÓGICA DE DISPARO DO SQUIB
  // -------------------------------------------------------------
  float pressao = altimetro.readPressure() / 100.0;
  float altitudeAtual = altimetro.readAltitude(pressaoBase);

  if (altitudeAtual > maxAltitudeAtingida) {
    maxAltitudeAtingida = altitudeAtual;
  }

  // Lógica de Detecção de Apogeu
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

  // Desliga o pino após o tempo configurado (sem travar o ESP32)
  if (squibAtivo && (tempoAtual - tempoInicioAcionamento >= TEMPO_PULSO_SQUIB)) {
    digitalWrite(PINO_SQUIB, LOW);
    squibAtivo = false;
    Serial.println(">>> Pulso do Squib encerrado por segurança.");
  }

  // -------------------------------------------------------------
  // 2. BNO055: LEITURAS INERCIAIS OTIMIZADAS
  // -------------------------------------------------------------
  sensors_event_t accelData;
  bno.getEvent(&accelData, Adafruit_BNO055::VECTOR_LINEARACCEL);
  imu::Quaternion quat = bno.getQuat();

  // Define string do status do Squib para o log
  String statusSquibStr = "AGUARDANDO";
  if (squibAtivo) statusSquibStr = "DISPARANDO";
  else if (acionado) statusSquibStr = "DISPARADO";

  // -------------------------------------------------------------
  // 3. GRAVAÇÃO NO CARTÃO SD (FORMATO CSV)
  // -------------------------------------------------------------
  File logFile = SD.open("/telemetria.csv", FILE_APPEND);
  if (logFile) {
    // Escreve linha formatada
    logFile.printf("%lu,%.2f,%.2f,%.2f,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                   tempoAtual,
                   altitudeAtual,
                   maxAltitudeAtingida,
                   pressao,
                   statusSquibStr.c_str(),
                   accelData.acceleration.x,
                   accelData.acceleration.y,
                   accelData.acceleration.z,
                   quat.w(),
                   quat.x(),
                   quat.y(),
                   quat.z());
    logFile.close(); // Garante o salvamento físico no SD
  } else {
    Serial.println("-> [ERRO SD] Falha ao abrir /telemetria.csv para gravação!");
  }

  // -------------------------------------------------------------
  // 4. EXIBIÇÃO NO SERIAL CONSOLE
  // -------------------------------------------------------------
  Serial.println("--------------------------------------------------");
  Serial.printf("[ALTITUDE] Atual: %.2f m | Máx: %.2f m | Status SKIB: %s\n", 
                altitudeAtual, maxAltitudeAtingida, statusSquibStr.c_str());

  Serial.printf("[ACCEL LIN] X: %.2f | Y: %.2f | Z: %.2f m/s²\n",
                accelData.acceleration.x, accelData.acceleration.y, accelData.acceleration.z);

  Serial.printf("[QUATERNION] W: %.2f | X: %.2f | Y: %.2f | Z: %.2f\n",
                quat.w(), quat.x(), quat.y(), quat.z());

  uint8_t sys, gyro, accel, mag = 0;
  bno.getCalibration(&sys, &gyro, &accel, &mag);
  Serial.printf("[CALIBRAÇÃO] SYS: %d | GYRO: %d | ACCEL: %d | MAG: %d\n", sys, gyro, accel, mag);

  delay(SAMPLE_RATE_MS);
}