//*
=================================================================================
  PROJETO: Receptor LoRa EBYTE E22 (Estação de Solo)
=================================================================================
  ESQUEMA DE LIGAÇÃO:
    LoRa E22 (UART Serial1):
      - M0    -> GND (Hardware)
      - M1    -> GND (Hardware)
      - RXD   -> GPIO D32 (TX do ESP32)
      - TXD   -> GPIO D33 (RX do ESP32)
      - AUX   -> GPIO D25
=================================================================================
*/

#include <Arduino.h>
#include <LoRa_E22.h> // Biblioteca ajustada para o módulo E22

#ifndef UNCONFIGURED
  #define UNCONFIGURED 255
#endif

#define ESP32_RX_PIN 33 // Recebe do TXD do E22
#define ESP32_TX_PIN 32 // Envia para o RXD do E22
#define PIN_E22_AUX  25 

HardwareSerial loraSerial(1);
LoRa_E22 e22(&loraSerial, PIN_E22_AUX, UNCONFIGURED, UNCONFIGURED);

// Estrutura RIGOROSAMENTE idêntica à do Transmissor
struct __attribute__((packed)) PacoteDados {
  char identificador[10];
  int idMensagem;
  float temperatura;
  float altitudeAtual;
  float altitudeMax;
  uint8_t statusSquib;
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INICIALIZANDO RECEPTOR TELEMETRIA LORA E22 ===");

  loraSerial.begin(9600, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);
  e22.begin();

  Serial.println("Sucesso: Receptor LoRa E22 pronto e aguardando pacotes...");
  Serial.println("====================================================\n");
}

void loop() {
  if (e22.available() > 0) {
    
    // Recebe a mensagem mapeada para o tamanho da struct
    ResponseStructContainer rsc = e22.receiveMessage(sizeof(PacoteDados));
    
    // Na biblioteca E22, código 1 representa SUCESSO na recepção
    if (rsc.status.code == 1) {
      PacoteDados dados = *(PacoteDados*) rsc.data;

      // Converte código numérico do Squib para texto legível
      String strStatusSquib = "AGUARDANDO APOGEU";
      if (dados.statusSquib == 1) strStatusSquib = "!!! DISPARANDO SQUIB !!!";
      else if (dados.statusSquib == 2) strStatusSquib = "DISPARADO (CONCLUÍDO)";

      // Impressão limpa no Monitor Serial
      Serial.println("================ PACOTE RECEBIDO ================");
      Serial.printf("ID do Pacote:     #%d (%s)\n", dados.idMensagem, dados.identificador);
      Serial.printf("Altitude Atual:   %.2f m\n", dados.altitudeAtual);
      Serial.printf("Altitude Máxima:  %.2f m\n", dados.altitudeMax);
      Serial.printf("Temperatura BMP:  %.1f °C\n", dados.temperatura);
      Serial.printf("Status do Squib:  %s\n", strStatusSquib.c_str());
      Serial.println("=================================================\n");

    } else {
      Serial.print("Erro na recepção: ");
      Serial.println(rsc.status.getResponseDescription());
    }

    // Libera a memória alocada pelo container de recepção
    rsc.close();
  }
  
  delay(10);
}
