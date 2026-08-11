/**
 * ============================================================================
 * PROJETO: Transmissor LoRa EBYTE E22 (Com Detecção de Presença Física)
 * ============================================================================
 * MAPEAMENTO DE PINOS (ESP32 <-> E22):
 * M0   -> GND (Nível Lógico LOW fixo)
 * M1   -> GND (Nível Lógico LOW fixo)
 * TXD do E22 -> GPIO 33 (RX do ESP32 - Pino de escuta)
 * RXD do E22 -> GPIO 32 (TX do ESP32 - Pino de envio)
 * AUX        -> GPIO 25 (Usado para monitorar presença e estado do rádio)
 * VCC        -> Fonte Externa (5V)
 * GND        -> GND Comum
 * ============================================================================
 */

#include <Arduino.h> 
#include <LoRa_E22.h> // Biblioteca atualizada para a linha E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

// MAPEAMENTO DE PINOS:
#define ESP32_RX_PIN 33 // Conectado ao TXD do E22
#define ESP32_TX_PIN 32 // Conectado ao RXD do E22
#define PIN_E22_AUX  25 

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

struct __attribute__((packed)) PacoteDados {
  char identificador[10];
  int32_t idMensagem;    // int32_t garante tamanho fixo de 4 bytes
  float temperatura;      
  float altitude;         
};

int32_t contador = 0;

// Função para verificar se o módulo E22 está fisicamente conectado
bool moduloEstaConectado() {
  // Se o módulo estiver energizado e pronto, o pino AUX fica em HIGH (3.3V)
  return digitalRead(PIN_E22_AUX) == HIGH;
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  // Configura o pino AUX com Pull-Down interno no ESP32
  pinMode(PIN_E22_AUX, INPUT_PULLDOWN);

  // Inicializa a UART do ESP32: (Baudrate, Config, RX_ESP, TX_ESP)
  loraSerial.begin(9600, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);

  e22.begin();

  Serial.println("\n====================================");
  if (moduloEstaConectado()) {
    Serial.println("SUCESSO: Módulo LoRa E22 detectado e pronto!");
  } else {
    Serial.println("ALERTA: Módulo LoRa E22 NÃO detectado. Verifique os fios e a alimentação!");
  }
  Serial.println("====================================");
}

void loop() {
  Serial.println("\n------------------------------------");

  // Só tenta enviar se o módulo estiver fisicamente conectado
  if (!moduloEstaConectado()) {
    Serial.println("[ERRO CRÍTICO] Módulo LoRa E22 desconectado ou sem energia! Envio cancelado.");
  } else {
    
    // Montagem do pacote de dados
    PacoteDados meusDados;
    memset(meusDados.identificador, 0, sizeof(meusDados.identificador));
    strcpy(meusDados.identificador, "DATA_LOG");
    meusDados.idMensagem = contador;
    meusDados.temperatura = 24.5 + (random(-50, 50) * 0.01);
    meusDados.altitude = 850.32 + (contador * 0.1); 

    Serial.printf("[TX E22] Enviando Pacote #%d via LoRa...\n", meusDados.idMensagem);
    Serial.printf("ID Texto: %s | Temp: %.2f C | Alt: %.2f m\n", 
                  meusDados.identificador, meusDados.temperatura, meusDados.altitude);

    // Envia o pacote de dados via rádio
    ResponseStatus rs = e22.sendMessage(&meusDados, sizeof(PacoteDados));

    Serial.print("Status do Envio Local: ");
    Serial.println(rs.getResponseDescription());

    contador++;
  }

  delay(3000); // Intervalo de 3 segundos entre envios
}