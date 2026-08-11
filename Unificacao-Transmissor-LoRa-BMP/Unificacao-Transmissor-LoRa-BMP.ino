/*
=================================================================================
  PROJETO: Transmissor Aviônica (ESP32 + BMP280 + LoRa E22)
=================================================================================
  ESQUEMA DE LIGAÇÃO:
    BMP280 (I2C):
      - SCL   -> GPIO D22
      - SDA   -> GPIO D21
    Squib (MOSFET):
      - Gate  -> GPIO D12
    LoRa E22 (UART Serial1):
      - M0    -> GND (Hardware)
      - M1    -> GND (Hardware)
      - RXD   -> GPIO D32 (TX do ESP32)
      - TXD   -> GPIO D33 (RX do ESP32)
      - AUX   -> GPIO D25
=================================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <LoRa_E22.h> // Biblioteca ajustada para o módulo E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

// --- PINOS ---
#define PINO_SQUIB        12       // MOSFET de acionamento do Apogeu
#define ESP32_RX_PIN      33       // Recebe do TXD do E22
#define ESP32_TX_PIN      32       // Envia para o RXD do E22
#define PIN_E22_AUX       25       // Pino de status AUX

// --- CONFIGURAÇÕES DE APOGEU ---
#define ALTITUDE_ALVO     0.4      // Altitude mínima (m) para habilitar disparo
#define QUEDA_MARGEM      1.5      // Queda necessária (m) a partir do pico
#define TEMPO_PULSO_SQUIB 2000     // Tempo do pulso em ms (2 segundos)

// --- OBJETOS ---
Adafruit_BMP280 altimetro;
HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

// --- ESTRUTURA DE TELEMETRIA LORA ---
struct __attribute__((packed)) PacoteDados {
  char identificador[10]; // "TELEMETRIA"
  int idMensagem;         // Contador de pacotes
  float temperatura;      // °C (BMP280)
  float altitudeAtual;    // m (BMP280)
  float altitudeMax;      // m (BMP280)
  uint8_t statusSquib;    // 0: AGUARDANDO, 1: DISPARANDO, 2: DISPARADO
};

// --- VARIÁVEIS GLOBAIS ---
float pressaoBase = 0.0;
float maxAltitudeAtingida = 0.0;
bool acionado = false;
bool squibAtivo = false;
unsigned long tempoInicioAcionamento = 0;

int contadorPacotes = 0;
unsigned long ultimoEnvioLoRa = 0;
const unsigned long INTERVALO_LORA = 200; // Envia telemetria a cada 200ms (5 Hz)

void setup() {
  // 1. Pino de segurança do acionador
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO TRANSMISSOR AVIONICA (BMP280 + LORA E22) ===");

  // 2. Inicialização do BMP280
  Wire.begin(21, 22);
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

  // 3. Inicialização do LoRa E22
  loraSerial.begin(9600, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);
  e22.begin();
  Serial.println("LoRa E22 Inicializado!");

  Serial.println("=========================================================\n");
}

void loop() {
  unsigned long tempoAtual = millis();

  // -------------------------------------------------------------
  // 1. LEITURA DO BMP280 E LÓGICA DE DETECÇÃO DE APOGEU
  // -------------------------------------------------------------
  float temp = altimetro.readTemperature();
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

  // Desliga o pino após o tempo configurado
  if (squibAtivo && (tempoAtual - tempoInicioAcionamento >= TEMPO_PULSO_SQUIB)) {
    digitalWrite(PINO_SQUIB, LOW);
    squibAtivo = false;
    Serial.println(">>> Pulso do Squib encerrado por segurança.");
  }

  // -------------------------------------------------------------
  // 2. TRANSMISSÃO LORA E22 (A CADA 200 MS / 5 HZ)
  // -------------------------------------------------------------
  if (tempoAtual - ultimoEnvioLoRa >= INTERVALO_LORA) {
    ultimoEnvioLoRa = tempoAtual;

    PacoteDados pacote;
    memset(pacote.identificador, 0, sizeof(pacote.identificador));
    strcpy(pacote.identificador, "TELEMETRIA");
    pacote.idMensagem = contadorPacotes;
    pacote.temperatura = temp;
    pacote.altitudeAtual = altitudeAtual;
    pacote.altitudeMax = maxAltitudeAtingida;

    if (squibAtivo) pacote.statusSquib = 1;      // DISPARANDO
    else if (acionado) pacote.statusSquib = 2;   // DISPARADO
    else pacote.statusSquib = 0;                 // AGUARDANDO

    // Envio do pacote via rádio E22
    ResponseStatus rs = e22.sendMessage(&pacote, sizeof(PacoteDados));

    // Exibição local
    Serial.printf("[TX E22 #%d] Alt: %.2fm | Max: %.2fm | Temp: %.1fC | Squib Status: %d | LoRa: %s\n",
                  contadorPacotes, altitudeAtual, maxAltitudeAtingida, temp, pacote.statusSquib, rs.getResponseDescription().c_str());

    contadorPacotes++;
  }
}
