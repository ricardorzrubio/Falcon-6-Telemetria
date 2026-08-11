/*
=================================================================================
  SISTEMA COMPLETO DE TELEMETRIA, APOGEU E DATALOGGER — ESP32 + LORA E22
=================================================================================
  Periféricos e Pinos:
    - Cartão SD (SPI):     CS = GPIO 5 (MOSI=23, MISO=19, SCK=18)
    - GPS (UART Serial2):  TX GPS -> GPIO D16 (RX2), RX GPS -> GPIO D17 (TX2)
    - BMP280 (I2C 0x76):   SDA = GPIO D21, SCL = GPIO D22
    - BNO055 (I2C 0x29):   SDA = GPIO D21, SCL = GPIO D22
    - Squib / SKIB:        MOSFET -> GPIO D13
    - LoRa E22 (UART1):    RXD -> GPIO D32 (TX1), TXD -> GPIO D33 (RX1), AUX -> GPIO D25
                           M0 -> GND, M1 -> GND
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

// LORA E22
#include <LoRa_E22.h>

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

// SD
const int chipSelect = 5;          // CS do Cartão SD (SPI)

// GPS NEO 6M
#define GPS_RX_PIN        16       // Recebe do TX do GPS (RX2)
#define GPS_TX_PIN        17       // Envia para o RX do GPS (TX2)
TinyGPSPlus gps;
#define gpsSerial Serial2

// BMP280 e BNO055
#define SDA_PIN           21       // I2C SDA
#define SCL_PIN           22       // I2C SCL

// BMP280
#define PINO_SQUIB        13       // Gate do MOSFET (Acionador Apogeu)
#define ALTITUDE_ALVO     0.4      // Altitude mínima (m) para liberar disparo
#define QUEDA_MARGEM      1.5      // Queda (m) a partir do pico para confirmar apogeu
#define TEMPO_PULSO_SQUIB 2000     // Duração do pulso em ms (2 segundos)
Adafruit_BMP280 altimetro;

// BNO055
#define SAMPLE_RATE_MS    100      // Frequência de 10 Hz (100 ms)
#define SENSOR_ID_BNO     55
Adafruit_BNO055 bno = Adafruit_BNO055(SENSOR_ID_BNO, 0x29, &Wire);

// LORA E22
#define LORA_RX_PIN       33       // RX ESP32 (Conectado ao TXD do E22)
#define LORA_TX_PIN       32       // TX ESP32 (Conectado ao RXD do E22)
#define PIN_E22_AUX       25       // AUX do E22
// O pacote COMPLETO (47 campos, ~151 bytes) só é gravado no SD (10Hz, sem
// custo de rádio). Pelo LoRa vai um pacote RESUMIDO de 55 bytes (posição,
// altitude, velocidade, status do squib e o essencial do BNO055: orientação
// + aceleração linear). Isso deixa o tempo de ar bem menor (~180ms de
// payload a 2.4kbps) e permite um intervalo de envio mais curto/confiável.
#define LORA_INTERVALO_MS 400      // pacote resumido a ~2,5 Hz
HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

// ============================================================================
//  >>> BLOCO DE VERIFICAÇÃO DE COMPONENTES (fácil de tirar) <<<
// ============================================================================
//  - VERIFICACAO_COMPONENTES = false  ->  desliga TODAS as checagens abaixo
//    (volta ao comportamento antigo, sem travar em nada)
//  - PARAR_SE_LORA_FALHAR = true      ->  também trava o código se o AUX do
//    LoRa não responder (por padrão fica em false, pois o LoRa é redundância
//    de telemetria e não deveria impedir o disparo do squib)
//
//  Para remover de vez: apague este bloco inteiro + as chamadas
//  verificarSD() / verificarGPS() / verificarBMP280() / verificarBNO055() /
//  verificarLoRa() dentro do setup().
// ============================================================================
#define VERIFICACAO_COMPONENTES true
#define PARAR_SE_LORA_FALHAR    false

// Trava o sistema e mostra qual componente falhou (usado pelos sensores críticos)
void falhaCritica(const char* componente, const char* motivo) {
  Serial.println("\n****************************************************");
  Serial.printf("!!! FALHA CRÍTICA NO COMPONENTE: %s\n", componente);
  Serial.printf("!!! MOTIVO: %s\n", motivo);
  Serial.println("!!! SISTEMA TRAVADO POR SEGURANÇA.");
  Serial.println("****************************************************\n");
  while (1) delay(10);
}

// Apenas avisa no Serial, sem travar (usado pelo LoRa por padrão)
void avisoNaoCritico(const char* componente, const char* motivo) {
  Serial.println("----------------------------------------------------");
  Serial.printf("[AVISO] %s: %s (sistema segue funcionando)\n", componente, motivo);
  Serial.println("----------------------------------------------------");
}

void verificarSD() {
  if (!SD.begin(chipSelect)) {
    falhaCritica("CARTAO SD", "SD.begin() falhou - verifique fiacao/CS (GPIO5) e o cartao");
  }
  Serial.println("[OK] Cartao SD detectado.");
}

void verificarGPS() {
  // Espera até 5s por qualquer caractere NMEA vindo do módulo (não exige fix, só fiação/energia)
  unsigned long inicio = millis();
  while (millis() - inicio < 5000) {
    while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
    if (gps.charsProcessed() > 10) break;
  }
  if (gps.charsProcessed() < 10) {
    falhaCritica("GPS NEO-6M", "Nenhum dado recebido em 5s - verifique fiacao RX/TX (GPIO16/17) e alimentacao");
  }
  Serial.println("[OK] GPS NEO-6M respondendo (aguardando fix de satelites).");
}

void verificarBMP280() {
  if (!altimetro.begin(0x76)) {
    falhaCritica("BMP280", "Nao encontrado no endereco I2C 0x76 - verifique SDA/SCL (GPIO21/22) e alimentacao");
  }
  Serial.println("[OK] BMP280 detectado (0x76).");
}

void verificarBNO055() {
  if (!bno.begin()) {
    falhaCritica("BNO055", "Nao encontrado no endereco I2C 0x29 - verifique SDA/SCL (GPIO21/22), ADR e alimentacao");
  }
  Serial.println("[OK] BNO055 detectado (0x29).");
}

void verificarLoRa() {
  delay(200); // dá tempo do módulo assentar o AUX após o begin()
  if (digitalRead(PIN_E22_AUX) != HIGH) {
    if (PARAR_SE_LORA_FALHAR) {
      falhaCritica("LORA E22", "Pino AUX nao respondeu HIGH - modulo desconectado ou sem energia");
    } else {
      avisoNaoCritico("LORA E22", "Pino AUX nao respondeu HIGH - modulo desconectado ou sem energia. Telemetria via SD continua normalmente.");
    }
    return;
  }
  Serial.println("[OK] LoRa E22 detectado (AUX em HIGH).");
}
// ============================================================================
//  >>> FIM DO BLOCO DE VERIFICAÇÃO DE COMPONENTES <<<
// ============================================================================

// --- VARIÁVEIS DE CONTROLE ---
float pressaoBase = 0.0;             // BMP280
float maxAltitudeAtingida = 0.0;     // BMP280
bool acionado = false;               // BMP280
bool squibAtivo = false;             // BMP280
unsigned long tempoInicioAcionamento = 0; // BMP280
unsigned long ultimoCiclo = 0;
unsigned long ultimoEnvioLoRa = 0;   // controla a taxa própria do LoRa (LORA_INTERVALO_MS)
uint32_t pacoteID = 0;               // Usado no CSV (coluna 1) e como idMensagem do LoRa

// --- PACOTE RESUMIDO PARA O LORA ---
// O SD já grava TUDO (47 colunas, 10Hz) - é a "caixa preta" completa do voo.
// Pelo rádio só vai o essencial para acompanhar o voo em tempo real: posição,
// altitude, velocidade, status do squib e o essencial do BNO055 (orientação +
// aceleração linear). Giroscópio bruto, magnetômetro, gravidade, quatérnions
// e calibração continuam só no SD - análise detalhada é feita depois do voo
// a partir do CSV.
struct __attribute__((packed)) TelemetriaResumida {
  uint32_t pacoteID;
  uint32_t tempoMs;
  float    latitude;
  float    longitude;
  float    altBMP;
  float    altMax;
  float    velocidadeGPS_mps;
  uint8_t  satelites;
  uint8_t  statusSquib;
  uint8_t  validadeLocalizacao;
  float    roll, pitch, yaw;        // graus (Euler) - orientação do BNO055
  float    accL_x, accL_y, accL_z;  // aceleração linear (sem gravidade)
};
// Trava a compilação se alguém mudar a struct e esquecer de atualizar o
// receptor/dashboard — é exatamente o bug que causava telemetria ilegível.
static_assert(sizeof(TelemetriaResumida) == 55,
              "Tamanho do TelemetriaResumida mudou! Atualize receptor.ino e o dashboard Python.");

void setup() {
  // 1. Pino de segurança do acionador
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO AVIONICA INTEGRADA (SD + GPS + BMP280 + BNO055 + LORA E22) TRANSMISSOR ===");

  // 2. GPS (UART2)
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // 3. LoRa E22 (UART1)
  pinMode(PIN_E22_AUX, INPUT_PULLDOWN);
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();
  Serial.println("LoRa E22 Inicializado!");

  // -------------------------------------------------------------
  //  CHAMADAS DE VERIFICAÇÃO (ver bloco acima - fácil de tirar)
  // -------------------------------------------------------------
  #if VERIFICACAO_COMPONENTES
    Serial.println("\n--- Verificando componentes ---");
    verificarSD();
    verificarLoRa();
  #else
    SD.begin(chipSelect); // mantém a inicialização mesmo com checagem desligada
  #endif

  // Cabeçalho CSV
  if (!SD.exists("/voo_telemetria.csv")) {
    File logFile = SD.open("/voo_telemetria.csv", FILE_WRITE);
    if (logFile) {
      logFile.println(
        "Pacote_ID,Tempo_ms,"
        "GPS_NEO_6M,Data,Hora_UTC,Latitude,Longitude,Alt_GPS_m,Velocidade_m/s,Satelites,HDOP,Curso_deg,Validade_Localizacao,"
        "BMP280,Alt_Max_m,Alt_BMP_m,Pressao_hPa,Temperatura_BMP_C,Status_Squib,"
        "BNO055,Roll_deg,Pitch_deg,Yaw_Heading_deg,Quat_x,Quat_y,Quat_z,Quat_w,"
        "Acc_L_x_m/s2,Acc_L_y_m/s2,Acc_L_z_m/s2,Acc_B_x_m/s2,Acc_B_y_m/s2,Acc_B_z_m/s2,"
        "Vel_Ang_x_rad/s,Vel_Ang_y_rad/s,Vel_Ang_z_rad/s,Mag_x_uT,Mag_y_uT,Mag_z_uT,"
        "Grav_x_m/s2,Grav_y_m/s2,Grav_z_m/s2,Temperatura_BNO_C,SYS,GYR,ACC,MAG"
      );
      logFile.close();
      Serial.println("Arquivo voo_telemetria.csv criado!");
    }
  }

  // 4. Barramento I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // BMP280
  #if VERIFICACAO_COMPONENTES
    verificarBMP280();
  #else
    if (!altimetro.begin(0x76)) {
      Serial.println("ERRO CRÍTICO: BMP280 não encontrado (0x76)!");
      while (1) delay(10);
    }
  #endif

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
  #if VERIFICACAO_COMPONENTES
    verificarBNO055();
  #else
    if (!bno.begin()) {
      Serial.println("ERRO CRÍTICO: BNO055 não encontrado (0x29)!");
      while (1) delay(10);
    }
  #endif
  delay(500);
  bno.setExtCrystalUse(true);
  Serial.println("BNO055 OK!");

  // GPS (checagem por último pois pode levar até 5s)
  #if VERIFICACAO_COMPONENTES
    verificarGPS();
  #endif

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
  float tempBMP = altimetro.readTemperature();

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

  // --- B. LEITURA BNO055 (todos os vetores) ---
  sensors_event_t orientData;   // Euler: heading(x), roll(y), pitch(z)
  sensors_event_t accelLinData; // aceleração linear (sem gravidade)
  sensors_event_t accelBrutoData; // aceleração bruta do acelerômetro (com gravidade)
  sensors_event_t gyroData;     // velocidade angular
  sensors_event_t magData;      // magnetômetro
  sensors_event_t gravData;     // vetor de gravidade

  bno.getEvent(&orientData,     Adafruit_BNO055::VECTOR_EULER);
  bno.getEvent(&accelLinData,   Adafruit_BNO055::VECTOR_LINEARACCEL);
  bno.getEvent(&accelBrutoData, Adafruit_BNO055::VECTOR_ACCELEROMETER);
  bno.getEvent(&gyroData,       Adafruit_BNO055::VECTOR_GYROSCOPE);
  bno.getEvent(&magData,        Adafruit_BNO055::VECTOR_MAGNETOMETER);
  bno.getEvent(&gravData,       Adafruit_BNO055::VECTOR_GRAVITY);

  imu::Quaternion quat = bno.getQuat();
  int8_t tempBNO = bno.getTemp();

  uint8_t sys, gyr, acc, mag;
  bno.getCalibration(&sys, &gyr, &acc, &mag);

  // --- C. TRATAMENTO DE DADOS DO GPS ---
  String dataStr = gps.date.isValid() ? String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) : "N/A";
  String horaStr = gps.time.isValid() ? String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) : "N/A";
  float latVal   = gps.location.isValid() ? gps.location.lat() : 0.0;
  float lngVal   = gps.location.isValid() ? gps.location.lng() : 0.0;
  float altGps   = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  float velGps   = gps.speed.isValid() ? gps.speed.mps() : 0.0;          // velocidade em m/s
  uint8_t sats   = gps.satellites.isValid() ? gps.satellites.value() : 0;
  float hdopVal  = gps.hdop.isValid() ? gps.hdop.value() / 100.0 : 0.0;
  float cursoVal = gps.course.isValid() ? gps.course.deg() : 0.0;
  const char* validadeLoc = gps.location.isValid() ? "SIM" : "NAO";

  // --- D. GRAVAÇÃO NO SD CARD ---
  File logFile = SD.open("/voo_telemetria.csv", FILE_APPEND);
  if (logFile) {
    logFile.printf(
      "%lu,%lu,"
      "%s,%s,%s,%.6f,%.6f,%.2f,%.2f,%d,%.2f,%.2f,%s,"
      "%s,%.2f,%.2f,%.2f,%.2f,%s,"
      "%s,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,"
      "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
      "%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,"
      "%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
      pacoteID, tempoAtual,
      "", dataStr.c_str(), horaStr.c_str(), latVal, lngVal, altGps, velGps, sats, hdopVal, cursoVal, validadeLoc,
      "", maxAltitudeAtingida, altitudeAtual, pressao, tempBMP, statusSquibStr.c_str(),
      "", orientData.orientation.y, orientData.orientation.z, orientData.orientation.x,
      quat.x(), quat.y(), quat.z(), quat.w(),
      accelLinData.acceleration.x, accelLinData.acceleration.y, accelLinData.acceleration.z,
      accelBrutoData.acceleration.x, accelBrutoData.acceleration.y, accelBrutoData.acceleration.z,
      gyroData.gyro.x, gyroData.gyro.y, gyroData.gyro.z,
      magData.magnetic.x, magData.magnetic.y, magData.magnetic.z,
      gravData.acceleration.x, gravData.acceleration.y, gravData.acceleration.z,
      tempBNO, sys, gyr, acc, mag
    );
    logFile.close();
  } else {
    Serial.println("-> [ERRO SD] Falha ao abrir /voo_telemetria.csv");
  }

  // --- E. TRANSMISSÃO LORA E22 (taxa própria: LORA_INTERVALO_MS) ---
  // O log no SD roda a 10Hz (acima), mas o envio pelo rádio é limitado a
  // LORA_INTERVALO_MS para controlar o tempo de ar do pacote resumido.
  bool horaDeEnviarLoRa = (tempoAtual - ultimoEnvioLoRa >= LORA_INTERVALO_MS);
  String loraStatusStr = "aguardando proximo envio";

  if (horaDeEnviarLoRa) {
    ultimoEnvioLoRa = tempoAtual;

    TelemetriaResumida pkt;
    pkt.pacoteID           = pacoteID;
    pkt.tempoMs             = tempoAtual;
    pkt.latitude            = latVal;
    pkt.longitude           = lngVal;
    pkt.altBMP              = altitudeAtual;
    pkt.altMax              = maxAltitudeAtingida;
    pkt.velocidadeGPS_mps   = velGps;
    pkt.satelites           = sats;
    pkt.statusSquib         = squibCode;
    pkt.validadeLocalizacao = gps.location.isValid() ? 1 : 0;
    pkt.roll   = orientData.orientation.y;
    pkt.pitch  = orientData.orientation.z;
    pkt.yaw    = orientData.orientation.x;
    pkt.accL_x = accelLinData.acceleration.x;
    pkt.accL_y = accelLinData.acceleration.y;
    pkt.accL_z = accelLinData.acceleration.z;

    ResponseStatus rs = e22.sendMessage(&pkt, sizeof(TelemetriaResumida));
    loraStatusStr = rs.getResponseDescription();
  }

  // --- F. MONITOR SERIAL COMPACTO ---
  Serial.printf("[#%u | %lums] BMP: %.1fm | Max: %.1fm | Lat: %.6f | Lng: %.6f | Sat: %d | Squib: %s | LoRa: %s\n",
                pacoteID, tempoAtual, altitudeAtual, maxAltitudeAtingida, latVal, lngVal, sats, statusSquibStr.c_str(), loraStatusStr.c_str());

  pacoteID++; // incrementa DEPOIS de usar em ambos (CSV e LoRa) - próximo pacote começa em 1
}
