/*
=================================================================================
  SISTEMA DE ACIONAMENTO DE APOGEU - ESP32 + BMP280
=================================================================================
  - Sensor: BMP280 (I2C 0x76)
  - Pino de Acionamento do Squib (via MOSFET): GPIO 12
  - Condição de Disparo: Altitude Relativa >= ALTITUDE_ALVO E detecção de queda
=================================================================================
             ESQUEMA DE LIGAÇÃO: BMP280 NO ESP32 (30 PINOS) - MODO I2C
=================================================================================
  BMP280  ->   ESP32 (30 Pinos)
---------------------------------------------------------------------------------
   VCC    ->   3V3 / 5V
   GND    ->   GND
   SCL    ->   GPIO D22
   SDA    ->   GPIO D21
   SKIB   ->   GPIO D13 (Conectado ao MOSFET do Squib)
=================================================================================
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#define PINO_SQUIB 13            // Pino GPIO 12 (SKIB) conectado ao Gate do MOSFET
#define ALTITUDE_ALVO 0.4        // Altitude mínima em metros para habilitar o disparo
#define QUEDA_MARGEM 1.5         // Queda em metros a partir do pico para confirmar apogeu
#define TEMPO_PULSO_SQUIB 2000   // Tempo de acionamento do Squib em ms (2 segundos)

Adafruit_BMP280 altimetro;
float pressaoBase;

float maxAltitudeAtingida = 0.0;
bool acionado = false;
bool squibAtivo = false;
unsigned long tempoInicioAcionamento = 0;

void setup() {
  // Configura o pino do acionador imediatamente como SAÍDA em NÍVEL BAIXO (Segurança)
  pinMode(PINO_SQUIB, OUTPUT);
  digitalWrite(PINO_SQUIB, LOW);

  Serial.begin(115200);
  delay(2000);

  if (!altimetro.begin(0x76)) {
    Serial.println("BMP280 não encontrado! Verifique as conexões.");
    while (1);
  }

  // Configurações avançadas do BMP280 para maior precisão e resposta rápida
  altimetro.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Modo de Operação */
                        Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                        Adafruit_BMP280::SAMPLING_X16,    /* Pressão oversampling */
                        Adafruit_BMP280::FILTER_X16,      /* Filtragem IIR */
                        Adafruit_BMP280::STANDBY_MS_63);  /* Tempo de standby */

  // Média inicial para calibrar a pressão no solo
  float somaPressao = 0;
  for (int i = 0; i < 10; i++) {
    somaPressao += altimetro.readPressure();
    delay(50);
  }
  pressaoBase = (somaPressao / 10.0) / 100.0;

  Serial.print("Pressão Base calibrada no solo: ");
  Serial.print(pressaoBase);
  Serial.println(" hPa");
}

void loop() {
  float pressao = altimetro.readPressure() / 100.0;
  float altitudeAtual = altimetro.readAltitude(pressaoBase);

  // Atualiza a altitude máxima atingida
  if (altitudeAtual > maxAltitudeAtingida) {
    maxAltitudeAtingida = altitudeAtual;
  }

  // Lógica de Detecção de Apogeu:
  // 1. Atingiu a altitude mínima de segurança (ALTITUDE_ALVO)
  // 2. A altitude atual caiu pelo menos "QUEDA_MARGEM" em relação ao pico máximo
  bool apogeuDetectado = (maxAltitudeAtingida >= ALTITUDE_ALVO) && 
                         (altitudeAtual <= (maxAltitudeAtingida - QUEDA_MARGEM));

  // Disparo do Squib
  if (apogeuDetectado && !acionado) {
    acionado = true;
    squibAtivo = true;
    tempoInicioAcionamento = millis(); // Grava o tempo em que disparou
    
    digitalWrite(PINO_SQUIB, HIGH);   // Liga o MOSFET / Squib
    Serial.println("!!! APOGEU DETECTADO - ACIONANDO SQUIB / SKIB !!!");
  }

  // Desliga o pino do Squib sem travar o ESP32 usando millis()
  if (squibAtivo && (millis() - tempoInicioAcionamento >= TEMPO_PULSO_SQUIB)) {
    digitalWrite(PINO_SQUIB, LOW);    // Desliga o acionador
    squibAtivo = false;
    Serial.println("Pulso do Squib finalizado por segurança.");
  }

  // Telemetria via Serial
  Serial.print("Altitude Atual: ");
  Serial.print(altitudeAtual);
  Serial.print(" m | Max Altitude: ");
  Serial.print(maxAltitudeAtingida);
  Serial.print(" m | Status Squib: ");
  if (squibAtivo) {
    Serial.println("DISPARANDO!");
  } else if (acionado) {
    Serial.println("DISPARADO (DESLIGADO)");
  } else {
    Serial.println("AGUARDANDO APOGEU");
  }

  delay(50); // Leitura a cada 50ms
}