/*
=================================================================================
  SISTEMA DE DATALOGGER GPS — ESP32 (30 PINOS) + MÓDULO SD + GPS (UART)
=================================================================================
  Esquema de Ligação:
---------------------------------------------------------------------------------
  Módulo SD (SPI):
    - CS   -> GPIO 5
    - MOSI -> GPIO 23
    - MISO -> GPIO 19
    - SCK  -> GPIO 18
  
  Módulo GPS (UART Serial2):
    - TX GPS -> GPIO D17 (RX do ESP32)
    - RX GPS -> GPIO D16 (TX do ESP32)
=================================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>

// --- CONFIGURAÇÕES DE PINOS ---
const int chipSelect = 5;       // Pino CS do Cartão SD (VSPI)
#define GPS_RX_PIN  16         // GPIO 16 (Recebe do TX do GPS)
#define GPS_TX_PIN  17          // GPIO 17 (Conecta no RX do GPS)

// --- OBJETOS ---
TinyGPSPlus gps;
#define gpsSerial Serial2

// --- TEMPORIZADOR DE GRAVAÇÃO ---
unsigned long ultimoSalvo = 0;
const unsigned long INTERVALO_GRAVACAO = 1000; // Salva no SD a cada 1 segundo (1000ms)

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO SYSTEMA DATALOGGER (GPS + SD) ===");

  // 1. Inicializa o GPS na Serial2
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS Serial inicializada. Aguardando sinal dos satélites...");

  // 2. Inicializa o Cartão SD
  if (!SD.begin(chipSelect)) {
    Serial.println("ERRO: Cartão SD não encontrado ou falha de conexão!");
    while (1); // Trava o código se o SD falhar
  }
  Serial.println("Cartão SD inicializado com sucesso.");

  // 3. Cria o cabeçalho no arquivo CSV se ele não existir
  if (!SD.exists("/gps_data.csv")) {
    File logFile = SD.open("/gps_data.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("Data,Hora,Lat,Lng,Satélites,Altitude_m,Velocidade_kmh");
      logFile.close();
      Serial.println("Arquivo gps_data.csv criado com o cabeçalho!");
    }
  }
}

void loop() {
  // 1. Processa os dados recebidos do GPS continuamente sem interrupções
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // 2. Alerta de falta de comunicação com o módulo GPS
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("ERRO: Nenhum GPS detectado! Verifique a fiação RX/TX."));
    while (true);
  }

  // 3. Grava no SD e exibe no Serial a cada INTERVALO_GRAVACAO (1s)
  if (millis() - ultimoSalvo >= INTERVALO_GRAVACAO) {
    ultimoSalvo = millis();
    processarEGravarDados();
  }
}

void processarEGravarDados() {
  // Monta a string no formato CSV para o SD
  String dataStr = "";
  String horaStr = "";
  String latStr = "0.0";
  String lngStr = "0.0";

  // Data
  if (gps.date.isValid()) {
    char buf[12];
    sprintf(buf, "%02d/%02d/%04d", gps.date.day(), gps.date.month(), gps.date.year());
    dataStr = String(buf);
  } else {
    dataStr = "Inválido";
  }

  // Hora (UTC)
  if (gps.time.isValid()) {
    char buf[10];
    sprintf(buf, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    horaStr = String(buf);
  } else {
    horaStr = "Inválido";
  }

  // Coordenadas
  if (gps.location.isValid()) {
    latStr = String(gps.location.lat(), 6);
    lngStr = String(gps.location.lng(), 6);
  }

  // Linha formatada em CSV
  String linhaCSV = dataStr + "," +
                    horaStr + "," +
                    latStr + "," +
                    lngStr + "," +
                    String(gps.satellites.value()) + "," +
                    String(gps.altitude.meters(), 2) + "," +
                    String(gps.speed.kmph(), 2);

  // --- GRAVAÇÃO NO SD CARD ---
  File logFile = SD.open("/gps_data.csv", FILE_APPEND);
  if (logFile) {
    logFile.println(linhaCSV);
    logFile.close(); // Fecha para garantir a gravação no cartão fisicamente
    Serial.println("-> [SD OK] Dados salvos no arquivo gps_data.csv");
  } else {
    Serial.println("-> [SD ERRO] Falha ao abrir gps_data.csv para gravação!");
  }

  // --- MONITORA NO SERIAL CONSOLE ---
  Serial.println(F("-------------------------------------"));
  Serial.print("Data/Hora (UTC): "); Serial.print(dataStr); Serial.print(" "); Serial.println(horaStr);
  Serial.print("Latitude:        "); Serial.println(latStr);
  Serial.print("Longitude:       "); Serial.println(lngStr);
  Serial.print("Satélites:       "); Serial.println(gps.satellites.value());
  Serial.print("Altitude:        "); Serial.print(gps.altitude.meters()); Serial.println(" m");
  Serial.print("Velocidade:      "); Serial.print(gps.speed.kmph()); Serial.println(" km/h");
  Serial.println(F("-------------------------------------"));
}
