import time
import datetime
import math
import random
import os
import pandas as pd

CSV_PATH = "telemetria_guardada.csv"

# Colunas exatas esperadas pelo Dashboard_Final.py
COLUNAS_DASHBOARD = [
    "Hora_Recebimento", "Pacote_ID", "Tempo_ms", "Latitude", "Longitude",
    "Alt_BMP", "Alt_Max", "Velocidade_GPS", "Satélites", "Status_Squib",
    "Validade_Localizacao", "Roll", "Pitch", "Yaw",
    "Acc_Lin_X", "Acc_Lin_Y", "Acc_Lin_Z"
]

def salvar_registro_telemetria(novo_registro):
    df_novo = pd.DataFrame([novo_registro], columns=COLUNAS_DASHBOARD)
    hdr = not os.path.exists(CSV_PATH)
    df_novo.to_csv(CSV_PATH, mode='a', header=hdr, index=False)

lat_base = -19.6765
lon_base = -43.2120

print("🚀 Simulador de Telemetria Compatível com Dashboard Iniciado...")
pacote_id = 1
tempo_ms = 0
alt_max = 0.0
status_squib = 0
duracao_subida = 10.0
alt_apogeu = 350.0

start_time = time.time()

try:
    while True:
        t_decorrido = time.time() - start_time
        tempo_ms += 200  # Envio a cada ~200ms
        
        # Simulação de Trajetória
        if t_decorrido <= duracao_subida:
            alt_bmp = alt_apogeu * (1 - ((t_decorrido - duracao_subida) / duracao_subida)**2)
            acc_z = random.uniform(15.0, 35.0)
            roll = random.uniform(-5.0, 5.0)
            pitch = random.uniform(80.0, 90.0) # Subindo quase na vertical
        else:
            alt_bmp = max(0.0, alt_apogeu - (t_decorrido - duracao_subida) * 15.0)
            acc_z = random.uniform(-2.0, 2.0)
            roll = random.uniform(-180.0, 180.0)
            pitch = random.uniform(-30.0, 30.0)
            
        if alt_bmp > alt_max:
            alt_max = alt_bmp
            
        # Lógica do Squib (0: Aguardando, 1: Disparando, 2: Disparado)
        if alt_bmp >= (alt_apogeu - 10) and status_squib == 0:
            status_squib = 1
        elif status_squib == 1 and t_decorrido > duracao_subida + 1:
            status_squib = 2
            
        lat = lat_base + (t_decorrido * 0.00002) + random.uniform(-0.000005, 0.000005)
        lon = lon_base + (t_decorrido * 0.00002) + random.uniform(-0.000005, 0.000005)
        
        acc_x = random.uniform(-1.5, 1.5)
        acc_y = random.uniform(-1.5, 1.5)
        vel_gps = math.sqrt(acc_x**2 + acc_y**2 + acc_z**2) * 2.0 if t_decorrido <= duracao_subida else 15.0
        
        hora_atual = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        
        novo_registro = {
            "Hora_Recebimento": hora_atual,
            "Pacote_ID": pacote_id,
            "Tempo_ms": tempo_ms,
            "Latitude": lat,
            "Longitude": lon,
            "Alt_BMP": round(alt_bmp, 2),
            "Alt_Max": round(alt_max, 2),
            "Velocidade_GPS": round(vel_gps, 2),
            "Satélites": 12,
            "Status_Squib": status_squib,
            "Validade_Localizacao": 1,
            "Roll": round(roll, 2),
            "Pitch": round(pitch, 2),
            "Yaw": round(random.uniform(0, 360), 2),
            "Acc_Lin_X": round(acc_x, 2),
            "Acc_Lin_Y": round(acc_y, 2),
            "Acc_Lin_Z": round(acc_z, 2)
        }
        
        salvar_registro_telemetria(novo_registro)
        print(f"[{hora_atual}] Pacote #{pacote_id} gravado -> Alt: {alt_bmp:.1f}m")
        
        pacote_id += 1
        time.sleep(0.5)

except KeyboardInterrupt:
    print("\nSimulador encerrado.")
