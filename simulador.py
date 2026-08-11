import time
import datetime
import math
import random
import os
import pandas as pd

CSV_PATH = "telemetria_guardada.csv"

CSV_ASSUNTOS = {
    "telemetria_altura.csv": ["Hora_Recebimento", "Tempo_ms", "Pacote_ID", "Alt_BMP", "Alt_GPS", "Alt_Max"],
    "telemetria_aceleracao.csv": ["Hora_Recebimento", "Tempo_ms", "Pacote_ID", "Acc_X", "Acc_Y", "Acc_Z", "Velocidade"],
    "telemetria_gps.csv": ["Hora_Recebimento", "Tempo_ms", "Pacote_ID", "Latitude", "Longitude", "Satélites"],
    "telemetria_orientacao.csv": ["Hora_Recebimento", "Tempo_ms", "Pacote_ID", "Quat_W", "Quat_X", "Quat_Y", "Quat_Z"],
    "telemetria_status.csv": ["Hora_Recebimento", "Tempo_ms", "Pacote_ID", "Pressao", "Status_Squib"]
}

def salvar_registro_telemetria(novo_registro):
    # CSV Geral
    df_novo = pd.DataFrame([novo_registro])
    hdr = not os.path.exists(CSV_PATH)
    df_novo.to_csv(CSV_PATH, mode='a', header=hdr, index=False)
    
    # CSVs Específicos por Assunto
    for file_path, cols in CSV_ASSUNTOS.items():
        sub_registro = {col: novo_registro[col] for col in cols if col in novo_registro}
        df_sub = pd.DataFrame([sub_registro])
        hdr_sub = not os.path.exists(file_path)
        df_sub.to_csv(file_path, mode='a', header=hdr_sub, index=False)

lat_base = -19.6765
lon_base = -43.2120

print("🚀 Simulador de Telemetria Iniciado...")
pacote_id = 5
tempo_ms = 0
alt_max = 0.0
status_squib = 0
duracao_subida = 10.0
alt_apogeu = 350.0

start_time = time.time()

try:
    while True:
        t_decorrido = time.time() - start_time
        tempo_ms += 100
        
        if t_decorrido <= duracao_subida:
            alt_bmp = alt_apogeu * (1 - ((t_decorrido - duracao_subida) / duracao_subida)**2)
            acc_z = random.uniform(15.0, 35.0)
        else:
            alt_bmp = max(0.0, alt_apogeu - (t_decorrido - duracao_subida) * 15.0)
            acc_z = random.uniform(-10.0, -8.0)
            
        if alt_bmp > alt_max:
            alt_max = alt_bmp
            
        if alt_bmp >= (alt_apogeu - 10) and status_squib == 0:
            status_squib = 1
        elif status_squib == 1 and t_decorrido > duracao_subida + 1:
            status_squib = 2
            
        pressao = 1013.25 * ((1 - 2.25577e-5 * alt_bmp) ** 5.25588)
        lat = lat_base + (t_decorrido * 0.00002) + random.uniform(-0.000005, 0.000005)
        lon = lon_base + (t_decorrido * 0.00002) + random.uniform(-0.000005, 0.000005)
        
        acc_x = random.uniform(-1.5, 1.5)
        acc_y = random.uniform(-1.5, 1.5)
        acc_res = math.sqrt(acc_x**2 + acc_y**2 + acc_z**2)
        vel_calc = acc_res * 0.1
        
        hora_atual = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        
        novo_registro = {
            "Hora_Recebimento": hora_atual,
            "Tempo_ms": tempo_ms,
            "Pacote_ID": pacote_id,
            "Latitude": lat,
            "Longitude": lon,
            "Alt_GPS": alt_bmp + random.uniform(-2, 2),
            "Pressao": pressao,
            "Alt_BMP": alt_bmp,
            "Alt_Max": alt_max,
            "Acc_X": acc_x,
            "Acc_Y": acc_y,
            "Acc_Z": acc_z,
            "Satélites": 12,
            "Quat_W": 1.0,
            "Quat_X": 0.0,
            "Quat_Y": 0.0,
            "Quat_Z": 0.0,
            "Status_Squib": status_squib,
            "Velocidade": vel_calc
        }
        
        salvar_registro_telemetria(novo_registro)
        print(f"[{hora_atual}] Pacote #{pacote_id} registrado em todos os CSVs")
        
        pacote_id += 5
        time.sleep(0.2)

except KeyboardInterrupt:
    print("\nSimulador encerrado.")