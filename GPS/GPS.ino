/*
=================================================================================
  SISTEMA DE LEITURA GPS - ESP32 + NEO-6M
=================================================================================
  - Sensor: Módulo GPS NEO-6M (Interface UART / Serial2)
  - Pinos de Comunicação: RX no GPIO 16, TX no GPIO 17
  - Função: Extração de dados de localização, satélites, altitude, velocidade e tempo (UTC)
=================================================================================
              ESQUEMA DE LIGAÇÃO: GPS NO ESP32 (30 PINOS) - MODO UART
=================================================================================
  GPS    ->   ESP32 (30 Pinos)
---------------------------------------------------------------------------------
   VCC         ->   5V (ou VIN / 3V3)
   GND         ->   GND
   RX (do GPS) ->   GPIO D16
   TX (do GPS) ->   GPIO D17
=================================================================================
*/

#include <TinyGPSPlus.h>

TinyGPSPlus gps;
#define gpsSerial Serial2

void setup() {
  Serial.begin(115200);
  
  // Inicializa a Serial2 nos pinos RX=16 e TX=17 a 9600 bauds (padrão do NEO-6M)
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("Waiting for GPS fix and satellites...");
}

void loop() {
  // Lê continuamente os dados que chegam do GPS pela porta serial
  while (gpsSerial.available() > 0)
    if (gps.encode(gpsSerial.read()))
      displayLocationInfo();

  // Segurança: Se após 5 segundos não receber dados do GPS, avisa no monitor serial
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("No GPS detected: check wiring."));
    while (true);
  }

  delay(1000);
}

void displayLocationInfo() {
  Serial.println(F("-------------------------------------"));
  Serial.println("\n Location Info:");

  Serial.print("Latitude:  ");
  Serial.print(gps.location.lat(), 6);
  Serial.print(" ");
  Serial.println(gps.location.rawLat().negative ? "S" : "N");

  Serial.print("Longitude: ");
  Serial.print(gps.location.lng(), 6);
  Serial.print(" ");
  Serial.println(gps.location.rawLng().negative ? "W" : "E");

  Serial.print("Fix Quality: ");
  Serial.println(gps.location.isValid() ? "Valid" : "Invalid");

  Serial.print("Satellites: ");
  Serial.println(gps.satellites.value());

  Serial.print("Altitude:   ");
  Serial.print(gps.altitude.meters());
  Serial.println(" m");

  Serial.print("Speed:      ");
  Serial.print(gps.speed.kmph());
  Serial.println(" km/h");

  Serial.print("Course:     ");
  Serial.print(gps.course.deg());
  Serial.println("°");

  Serial.print("Date:       ");
  if (gps.date.isValid()) {
    Serial.printf("%02d/%02d/%04d\n", gps.date.day(), gps.date.month(), gps.date.year());
  } else {
    Serial.println("Invalid");
  }

  Serial.print("Time (UTC): ");
  if (gps.time.isValid()) {
    Serial.printf("%02d:%02d:%02d\n", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    Serial.println("Invalid");
  }

  Serial.println(F("-------------------------------------"));
}