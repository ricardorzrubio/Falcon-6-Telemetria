// ============================================================
//  BNO055 + ESP32 — Leitura Completa via Serial Console
//  Bibliotecas necessárias (instalar pelo Library Manager):
//    - Adafruit BNO055
//    - Adafruit Unified Sensor
// ============================================================
/*
=================================================================================
             ESQUEMA DE LIGAÇÃO: BNO055 NO ESP32 (30 PINOS) - MODO SPI
=================================================================================
  BNO055  ->   ESP32 (30 Pinos)  |  Motivo 
---------------------------------------------------------------------------------
   VCC        ->   5V (ou VIN / 3V3) | 
   GND        ->   GND               |  
   SCL   ->   GPIO D22       |  
   SDA      ->   GPIO D21          |   
=================================================================================
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h> //fornece os tipos de dados tridimensionais complexos que o sensor calcula
//A ecossistema Adafruit é o padrão da indústria.

// ============================================================
//  CONFIGURAÇÕES
//  Pinos I2C padrão do ESP32: SDA=21, SCL=22
//  Se usar outros pinos, altere abaixo e use Wire.begin(SDA, SCL)
// ============================================================
#define SDA_PIN        21
#define SCL_PIN        22
#define SAMPLE_RATE_MS 1000     // intervalo entre leituras (ms)
#define SENSOR_ID      55

// ============================================================
//  Objeto do sensor
//  Endereço padrão: 0x28 | com pino ADR em HIGH: 0x29
// ============================================================
Adafruit_BNO055 bno = Adafruit_BNO055(SENSOR_ID, 0x29, &Wire);

void printSeparator() {
  Serial.println("--------------------------------------------------");
}

void printCalibration() {
  uint8_t sys, gyro, accel, mag = 0;
  bno.getCalibration(&sys, &gyro, &accel, &mag);

  Serial.printf("Calibração  ->  SYS: %d  GYRO: %d  ACCEL: %d  MAG: %d\n",
                sys, gyro, accel, mag);
}

void setup() {
  Serial.begin(115200);
  delay(1500); // aguarda a serial estabilizar no ESP32

  Serial.println("\n=== BNO055 + ESP32 — Sistema de Navegação Inercial ===");
  printSeparator();

  // Inicia I2C nos pinos definidos
  Wire.begin(SDA_PIN, SCL_PIN);

  // Inicializa o sensor
  if (!bno.begin()) {
    Serial.println("ERRO: BNO055 não encontrado!");
    Serial.println("Verifique: alimentação 3.3V, cabos SDA/SCL e endereço I2C.");
    Serial.println("Pinos padrão ESP32 → SDA=GPIO21  SCL=GPIO22");
    //while (1) delay(10); // trava — sem sensor não há o que fazer
  }

  delay(1000);

  // Usa cristal externo para maior precisão
  bno.setExtCrystalUse(true);

  Serial.println("Sensor BNO055 inicializado com sucesso!");
  Serial.println("Aguarde calibração completa (SYS=3) para maior precisão.");
  printSeparator();
}

void loop() {

  // Declara estruturas de evento para cada vetor
  sensors_event_t orientData;
  sensors_event_t accelData;
  sensors_event_t gravData;
  sensors_event_t gyroData;
  sensors_event_t magData;

  // Lê todos os vetores disponíveis
  bno.getEvent(&orientData, Adafruit_BNO055::VECTOR_EULER);
  bno.getEvent(&accelData,  Adafruit_BNO055::VECTOR_LINEARACCEL);
  bno.getEvent(&gravData,   Adafruit_BNO055::VECTOR_GRAVITY);
  bno.getEvent(&gyroData,   Adafruit_BNO055::VECTOR_GYROSCOPE);
  bno.getEvent(&magData,    Adafruit_BNO055::VECTOR_MAGNETOMETER);

  imu::Quaternion quat = bno.getQuat();

  // Temperatura interna do chip
  int8_t temperature = bno.getTemp();

  printSeparator();

  // Ângulos de Euler
  Serial.println("[ ORIENTAÇÃO - Ângulos de Euler ]");
  Serial.printf("  Heading (X): %.4f graus\n", orientData.orientation.x);
  Serial.printf("  Roll    (Y): %.4f graus\n", orientData.orientation.y);
  Serial.printf("  Pitch   (Z): %.4f graus\n", orientData.orientation.z);

  // Aceleração linear
  Serial.println("[ ACELERAÇÃO LINEAR (sem gravidade) ]");
  Serial.printf("  X: %.4f m/s²\n", accelData.acceleration.x);
  Serial.printf("  Y: %.4f m/s²\n", accelData.acceleration.y);
  Serial.printf("  Z: %.4f m/s²\n", accelData.acceleration.z);

  // Vetor de gravidade
  Serial.println("[ VETOR DE GRAVIDADE ]");
  Serial.printf("  X: %.4f m/s²\n", gravData.acceleration.x);
  Serial.printf("  Y: %.4f m/s²\n", gravData.acceleration.y);
  Serial.printf("  Z: %.4f m/s²\n", gravData.acceleration.z);

  // Giroscópio
  Serial.println("[ GIROSCÓPIO - Velocidade Angular ]");
  Serial.printf("  X: %.4f rad/s\n", gyroData.gyro.x);
  Serial.printf("  Y: %.4f rad/s\n", gyroData.gyro.y);
  Serial.printf("  Z: %.4f rad/s\n", gyroData.gyro.z);

  // Magnetômetro
  Serial.println("[ MAGNETÔMETRO - Campo Magnético ]");
  Serial.printf("  X: %.4f µT\n", magData.magnetic.x);
  Serial.printf("  Y: %.4f µT\n", magData.magnetic.y);
  Serial.printf("  Z: %.4f µT\n", magData.magnetic.z);

  Serial.println("[ QUATERNION (fusão inercial) ]");
  Serial.printf("  W: %.4f  X: %.4f  Y: %.4f  Z: %.4f\n",
                quat.w(), quat.x(), quat.y(), quat.z());

  // Temperatura
  Serial.printf("[ TEMPERATURA DO CHIP ]  %d °C\n", temperature);

  // Calibração
  printCalibration();

  delay(SAMPLE_RATE_MS);
}

/*Ideia de loop mais otimizado:
void loop() {
  // 1. Aceleração Linear (Sem a força da gravidade)
  sensors_event_t accelData;
  bno.getEvent(&accelData, Adafruit_BNO055::VECTOR_LINEARACCEL);

  // 2. Orientação 3D pura (Quatérnion - sem Gimbal Lock)
  imu::Quaternion quat = bno.getQuat();

  // 3. Temperatura do chip
  int8_t temperature = bno.getTemp();

  printSeparator();

  // Exibição Limpa e Rápida
  Serial.printf("[ ACELERAÇÃO LINEAR (m/s²) ]  X: %.2f | Y: %.2f | Z: %.2f\n",
                accelData.acceleration.x, accelData.acceleration.y, accelData.acceleration.z);

  Serial.printf("[ QUATERNION ]                W: %.2f | X: %.2f | Y: %.2f | Z: %.2f\n",
                quat.w(), quat.x(), quat.y(), quat.z());

  Serial.printf("[ TEMPERATURA ]               %d °C\n", temperature);

  // Status da calibração
  printCalibration();

  delay(SAMPLE_RATE_MS);
}




*/





