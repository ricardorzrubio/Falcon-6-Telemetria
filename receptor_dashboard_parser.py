"""
====================================================================
  PARSER DO PACOTE BINÁRIO DO RECEPTOR LORA E22 (151 bytes)
====================================================================
  Lê os bytes brutos que o receptor ESP32 manda pela USB e devolve
  um dicionário com todos os 47 campos já nomeados e convertidos.

  O struct.unpack aqui precisa bater EXATAMENTE (mesma ordem, mesmos
  tipos) com o `TelemetriaPacket` do código do transmissor/receptor.
  Se um dia mudar o struct no ESP32, atualize PACKET_FORMAT aqui também.
====================================================================
"""

import struct
import serial

# ---------------------------------------------------------------
#  FORMATO DO PACOTE (little-endian, igual ao ESP32)
# ---------------------------------------------------------------
#  2I    -> pacoteID (uint32), tempoMs (uint32)
#  2B    -> gpsDia (uint8), gpsMes (uint8)
#  H     -> gpsAno (uint16)
#  3B    -> gpsHora, gpsMinuto, gpsSegundo (uint8 cada)
#  4f    -> latitude, longitude, altitudeGPS, velocidadeGPS_mps (float)
#  B     -> satelites (uint8)
#  2f    -> hdop, curso (float)
#  B     -> validadeLocalizacao (uint8, 0/1)
#  4f    -> altMax, altBMP, pressao, tempBMP (float)
#  B     -> statusSquib (uint8, 0/1/2)
#  22f   -> roll, pitch, yaw, quatX, quatY, quatZ, quatW,
#           accL_x, accL_y, accL_z, accB_x, accB_y, accB_z,
#           velAng_x, velAng_y, velAng_z, mag_x, mag_y, mag_z,
#           grav_x, grav_y, grav_z (float cada)
#  b     -> tempBNO (int8)
#  4B    -> calSys, calGyr, calAcc, calMag (uint8 cada)
# ---------------------------------------------------------------
PACKET_FORMAT = "<2I2BH3B4fB2fB4fB22fb4B"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)  # deve dar 151

# Nomes dos campos, na MESMA ordem em que aparecem no PACKET_FORMAT
FIELD_NAMES = [
    "pacoteID", "tempoMs",
    "gpsDia", "gpsMes", "gpsAno", "gpsHora", "gpsMinuto", "gpsSegundo",
    "latitude", "longitude", "altitudeGPS", "velocidadeGPS_mps",
    "satelites", "hdop", "curso", "validadeLocalizacao",
    "altMax", "altBMP", "pressao", "tempBMP", "statusSquib",
    "roll", "pitch", "yaw",
    "quatX", "quatY", "quatZ", "quatW",
    "accL_x", "accL_y", "accL_z",
    "accB_x", "accB_y", "accB_z",
    "velAng_x", "velAng_y", "velAng_z",
    "mag_x", "mag_y", "mag_z",
    "grav_x", "grav_y", "grav_z",
    "tempBNO",
    "calSys", "calGyr", "calAcc", "calMag",
]

STATUS_SQUIB_LABELS = {0: "AGUARDANDO", 1: "DISPARANDO", 2: "DISPARADO"}


def parse_packet(raw_bytes: bytes) -> dict:
    """Converte os bytes brutos (151 bytes) num dicionário nomeado."""
    valores = struct.unpack(PACKET_FORMAT, raw_bytes)
    dados = dict(zip(FIELD_NAMES, valores))

    # Conveniências: traduz códigos numéricos em texto
    dados["validadeLocalizacao"] = "SIM" if dados["validadeLocalizacao"] else "NAO"
    dados["statusSquib"] = STATUS_SQUIB_LABELS.get(dados["statusSquib"], "DESCONHECIDO")

    return dados


def ler_serial(porta: str, baudrate: int = 115200):
    """
    Gerador que fica lendo a porta serial do receptor e produz um
    dicionário de telemetria a cada pacote completo recebido.
    Uso:
        for dados in ler_serial("COM5"):
            print(dados["altBMP"], dados["latitude"], dados["statusSquib"])
    """
    with serial.Serial(porta, baudrate, timeout=2) as ser:
        while True:
            raw = ser.read(PACKET_SIZE)
            if len(raw) != PACKET_SIZE:
                # Timeout ou pacote incompleto - descarta e tenta de novo.
                # Se isso acontecer com frequência, considere adicionar bytes
                # de sincronismo (ex.: 0xAA 0x55) no início do pacote no ESP32
                # e procurar por eles aqui antes de ler os 151 bytes.
                continue
            yield parse_packet(raw)


if __name__ == "__main__":
    import sys

    porta_serial = sys.argv[1] if len(sys.argv) > 1 else "COM7"
    print(f"Tamanho esperado do pacote: {PACKET_SIZE} bytes")
    print(f"Lendo telemetria em {porta_serial}...\n")

    for dados in ler_serial(porta_serial):
        print(
            f"#{dados['pacoteID']:>5} | "
            f"Alt: {dados['altBMP']:6.1f}m (max {dados['altMax']:6.1f}m) | "
            f"Lat/Lng: {dados['latitude']:.5f}, {dados['longitude']:.5f} | "
            f"Sat: {dados['satelites']:2} | "
            f"Squib: {dados['statusSquib']:10} | "
            f"Cal SYS/GYR/ACC/MAG: {dados['calSys']}/{dados['calGyr']}/{dados['calAcc']}/{dados['calMag']}"
        )
