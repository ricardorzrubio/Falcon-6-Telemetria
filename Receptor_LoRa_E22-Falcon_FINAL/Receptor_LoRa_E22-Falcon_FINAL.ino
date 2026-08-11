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
  IMPORTANTE: a struct TelemetriaResumida abaixo tem que ser IDÊNTICA (mesma
  ordem, mesmos tipos) à do transmissor. Qualquer diferença desalinha os bytes.
  O static_assert abaixo trava a compilação se o tamanho mudar sem querer.

  Protocolo no USB (para o Python nunca perder o alinhamento mesmo se um
  byte for perdido na porta serial):
    [0xAA][0x55][ payload de sizeof(TelemetriaResumida) bytes ][checksum XOR]
=================================================================================
*/

#include <Arduino.h>
#include <LoRa_E22.h>

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

#define ESP32_RX_PIN 33
#define ESP32_TX_PIN 32
#define PIN_E22_AUX  25

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

// --- PACOTE RESUMIDO (tem que bater com o transmissor) ---
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
static_assert(sizeof(TelemetriaResumida) == 55,
              "Tamanho do TelemetriaResumida mudou! Atualize transmissor.ino e o dashboard Python.");

const uint8_t SYNC_BYTE_1 = 0xAA;
const uint8_t SYNC_BYTE_2 = 0x55;

uint8_t calcularChecksum(const uint8_t* data, size_t len) {
  uint8_t chk = 0;
  for (size_t i = 0; i < len; i++) chk ^= data[i];
  return chk;
}

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
    ResponseStructContainer rsc = e22.receiveMessage(sizeof(TelemetriaResumida));

    // status.code == 1 significa SUCESSO (recebeu exatamente os bytes esperados)
    if (rsc.status.code == 1) {
      TelemetriaResumida dados = *(TelemetriaResumida*) rsc.data;

      uint8_t frame[2 + sizeof(TelemetriaResumida) + 1];
      frame[0] = SYNC_BYTE_1;
      frame[1] = SYNC_BYTE_2;
      memcpy(&frame[2], &dados, sizeof(TelemetriaResumida));
      frame[2 + sizeof(TelemetriaResumida)] = calcularChecksum((uint8_t*)&dados, sizeof(TelemetriaResumida));

      Serial.write(frame, sizeof(frame));
    }

    rsc.close(); // Libera a memória RAM
  }
  delay(5);
}
