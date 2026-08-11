/*
=================================================================================
             ESQUEMA DE LIGAÇÃO: MÓDULO MICRO SD NO ESP32 (30 PINOS) - MODO SPI
=================================================================================
  Módulo SD   ->   ESP32 (30 Pinos)  |  Motivo / Explicação
---------------------------------------------------------------------------------
   VCC        ->   5V (ou VIN / 3V3) |  Alimentação (módulos comuns usam 5V).
   GND        ->   GND               |  Conexão de aterramento comum (Negativo).
   MISO       ->   GPIO D19           |  Master In Slave Out (Entrada de dados no ESP32).
   MOSI       ->   GPIO D23          |  Master Out Slave In (Saída de dados do ESP32).
   SCK (CLK)  ->   GPIO D18           |  Serial Clock (Sincronismo de tempo do SPI).
   CS (SS)    ->   GPIO D5            |  Chip Select (Seleciona o cartão para uso).
=================================================================================
  *Nota sobre VCC: Se o seu módulo tiver um chip regulador de tensão (pequeno chip
  de 3 pernas), ligue-o no pino 5V do ESP32. Se for uma placa pura, use o 3V3.
=================================================================================
=================================================================================
             SOBRE AS BIBLIOTECAS UTILIZADAS NESTE CÓDIGO (LEIA ISTO!)
=================================================================================
  * NÃO INSTALE NADA NO GERENCIADOR DE BIBLIOTECAS DA ARDUINO IDE PARA O CARTÃO SD! *
  
  As bibliotecas <SPI.h> e <SD.h> usadas neste projeto são "NATIVAS" (Core Libraries).
  Isso significa que elas já vêm embutidas de fábrica dentro do pacote de placas do 
  ESP32 (Espressif Systems) que está instalado na sua IDE.
  
  Quando você seleciona a placa "DOIT ESP32 DEVKIT V1" ou "ESP32 Dev Module" e clica 
  para compilar, a própria IDE busca automaticamente a versão secreta dessas 
  bibliotecas que foi escrita e otimizada especificamente para o chip do ESP32.
  
  O que acontece se você tentar instalar a biblioteca "SD" pelo gerenciador?
  A biblioteca do gerenciador foi feita para o Arduino Uno tradicional. Se você 
  instalá-la, gerará um conflito de arquivos e o código dará erro de compilação, 
  pois o computador tentará aplicar regras de um Arduino antigo no seu ESP32 moderno.
=================================================================================
*/

#include <SPI.h>  // Biblioteca nativa para comunicação SPI
#include <SD.h>   // Biblioteca nativa para controle de Cartões SD

// No ESP32, o pino padrão para o Chip Select (CS) da rede SPI principal é o GPIO 5
const int chipSelect = 5; 

File dataFile;    // Cria o objeto para manipular os arquivos

void setup()
{
  Serial.begin(115200);   // Inicializa o Monitor Serial
  delay(1500);            // Aguarda o monitor abrir para você não perder os textos na tela
  
  Serial.println("\n--- Iniciando Teste do Cartao SD ---");

  // Inicializa o cartão SD passando o pino CS correto do ESP32 (Pino 5)
  if (!SD.begin(chipSelect))
  {
    Serial.println("Erro: Cartao SD nao encontrado ou conexoes incorretas!");
    while (1); // Trava o código aqui caso o cartão não seja detectado
  }

  Serial.println("Sucesso: Cartao SD inicializado corretamente.");

  // Abre o arquivo para escrita. 
  // IMPORTANTE: No ESP32, sempre use a barra "/" antes do nome do arquivo para indicar a raiz.
  dataFile = SD.open("/teste.txt", FILE_WRITE);

  // Se o arquivo foi aberto/criado com sucesso:
  if (dataFile)
  {
    dataFile.println("Teste de escrita no SD com ESP32!"); // Escreve o texto dentro do arquivo
    dataFile.close();                                      // Fecha o arquivo (salva os dados fisicamente)

    Serial.println("Sucesso: Texto gravado e arquivo salvo!");
  }
  else
  {
    Serial.println("Erro ao abrir ou criar o arquivo teste.txt");
  }
}

void loop()
{
  // Deixamos o loop vazio porque só queremos gravar o teste uma única vez no boot
}
