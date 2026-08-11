/*
=================================================================================
  RECEPTOR LORA E22 — ESTAÇÃO DE SOLO (USB BINÁRIO PARA DASHBOARD PYTHON)
=================================================================================
  Mapeamento de Pinos:
    - TXD do LoRa E22 -> GPIO 33 do ESP32 (RX1)
    - RXD do LoRa E22 -> GPIO 32 do ESP32 (TX1)
    - AUX do LoRa E22 -> GPIO 25 do ESP32
    - M0 e M1        -> GND (Fixo)
=================================================================================
*/

#include <Arduino.h>
#include <LoRa_E22.h> // Biblioteca ajustada para E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

#define ESP32_RX_PIN 33 // Conectado ao TXD do E22
#define ESP32_TX_PIN 32 // Conectado ao RXD do E22
#define PIN_E22_AUX  25 // Conectado ao AUX do E22

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

// Estrutura exatamente igual (53 bytes packed)
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
  uint8_t statusSquib;
};

void setup() {
  // Comunicação de alta velocidade com o Python/Streamlit via USB
  Serial.begin(115200);
  delay(1000);

  // Inicializa comunicação UART1 com o rádio LoRa E22 a 9600 baud
  loraSerial.begin(9600, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);
  e22.begin();
}

void loop() {
  if (e22.available() > 0) {
    ResponseStructContainer rsc = e22.receiveMessage(sizeof(TelemetriaPacket));
    
    // Na biblioteca E22, status.code == 1 significa SUCESSO
    if (rsc.status.code == 1) {
      TelemetriaPacket dados = *(TelemetriaPacket*) rsc.data;

      // Envia os bytes binários brutos diretamente para a porta USB
      Serial.write((uint8_t*)&dados, sizeof(TelemetriaPacket));
    }

    rsc.close(); // Libera a memória RAM
  }
  delay(5);
}