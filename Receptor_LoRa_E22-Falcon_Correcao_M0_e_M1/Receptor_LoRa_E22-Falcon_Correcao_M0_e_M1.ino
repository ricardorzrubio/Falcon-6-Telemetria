/**
 * ============================================================================
 * PROJETO: Receptor LoRa EBYTE E22 (M0 e M1 fixos no GND)
 * ============================================================================
 * MAPEAMENTO DE PINOS SOLICITADO:
 * TXD do LoRa -> GPIO 33 do ESP32 (Pino RX1 da Serial interna)
 * RXD do LoRa -> GPIO 32 do ESP32 (Pino TX1 da Serial interna)
 * AUX do LoRa -> GPIO 25 do ESP32
 * M0 / M1     -> GND (Fixo)
 * VCC         -> Fonte Externa (5V)
 * GND         -> GND Comum
 * ============================================================================
 */

#include <Arduino.h> 
#include <LoRa_E22.h> // Biblioteca para módulos EBYTE E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

// MAPEAMENTO EXATO DOS PINOS
#define ESP32_RX_PIN 33 // Conectado ao TXD do LoRa E22
#define ESP32_TX_PIN 32 // Conectado ao RXD do LoRa E22
#define PIN_E22_AUX  25 // Conectado ao AUX do LoRa E22

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

struct __attribute__((packed)) PacoteDados {
  char identificador[10];
  int32_t idMensagem;     
  float temperatura;      
  float altitude;         
};

unsigned long tempoUltimoAviso = 0;

bool moduloEstaConectado() {
  return digitalRead(PIN_E22_AUX) == HIGH;
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  pinMode(PIN_E22_AUX, INPUT_PULLDOWN);

  // loraSerial.begin(baudrate, config, rxPin, txPin)
  loraSerial.begin(9600, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);

  e22.begin();

  Serial.println("\n====================================");
  if (moduloEstaConectado()) {
    Serial.println("SUCESSO: Receptor LoRa E22 detectado no D33/D32/D25!");
  } else {
    Serial.println("ALERTA: Módulo LoRa E22 NÃO detectado. Verifique os fios e a alimentação!");
  }
  Serial.println("====================================");
}

void loop() {
  if (!moduloEstaConectado()) {
    Serial.println("[ERRO CRÍTICO] Módulo LoRa E22 desconectado ou sem energia!");
    delay(2000);
    return;
  }

  if (millis() - tempoUltimoAviso > 5000) {
    tempoUltimoAviso = millis();
    Serial.println("[RX E22] Escutando a frequência... (Aguardando pacotes)");
  }

  if (e22.available() > 0) {
    ResponseStructContainer rc = e22.receiveMessage(sizeof(PacoteDados));

    if (rc.status.code == 1) { // 1 = SUCESSO
      PacoteDados dadosRecebidos = *(PacoteDados*) rc.data;

      Serial.println("\n------------------------------------");
      Serial.printf("[RX E22] Pacote #%d Recebido!\n", dadosRecebidos.idMensagem);
      Serial.printf("ID Texto: %s | Temp: %.2f C | Alt: %.2f m\n", 
                    dadosRecebidos.identificador, 
                    dadosRecebidos.temperatura, 
                    dadosRecebidos.altitude);
    } else {
      Serial.print("[ERRO RX E22] Falha na leitura: ");
      Serial.println(rc.status.getResponseDescription());
    }

    rc.close(); // Libera a memória RAM
  }

  delay(10); 
}