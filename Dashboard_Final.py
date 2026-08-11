import streamlit as st
import serial
import serial.tools.list_ports
import pandas as pd
import numpy as np
import struct
import time
import datetime
import sqlite3
import os
import folium
from streamlit_folium import st_folium
import plotly.graph_objects as go
import plotly.express as px

st.set_page_config(
    page_title="Estação de Solo - Aviônica LoRa",
    page_icon="🚀",
    layout="wide"
)

st.title("🚀 Estação de Solo - (Offline)")

# =============================================================================
# --- PACOTE RESUMIDO DO LORA (TelemetriaResumida) — 16 CAMPOS, 55 BYTES ---
# Tem que bater exatamente com a struct TelemetriaResumida do transmissor
# e do receptor (mesma ordem, mesmos tipos). Traz posição/altitude/
# velocidade/status do squib e o essencial do BNO055 (orientação +
# aceleração linear) para os gráficos em tempo real. Giroscópio bruto,
# magnetômetro, gravidade e calibração continuam só no SD; use a seção
# "Análise Pós-Voo" no fim da página para ver esses dados.
# =============================================================================
FIELDS = [
    ("Pacote_ID",             "I"),  # uint32_t
    ("Tempo_ms",               "I"),  # uint32_t
    ("Latitude",               "f"),
    ("Longitude",              "f"),
    ("Alt_BMP",                "f"),
    ("Alt_Max",                "f"),
    ("Velocidade_GPS",         "f"),
    ("Satélites",              "B"),  # uint8_t
    ("Status_Squib",           "B"),  # uint8_t (0/1/2)
    ("Validade_Localizacao",   "B"),  # uint8_t (0/1)
    ("Roll",                   "f"),  # graus - orientação BNO055
    ("Pitch",                  "f"),
    ("Yaw",                    "f"),
    ("Acc_Lin_X",              "f"),  # m/s² - aceleração linear BNO055
    ("Acc_Lin_Y",              "f"),
    ("Acc_Lin_Z",              "f"),
]

STRUCT_FORMAT = "<" + "".join(t for _, t in FIELDS)
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
FIELD_NAMES = [n for n, _ in FIELDS]

# --- Protocolo de quadro no USB: [0xAA][0x55][payload][checksum XOR] ---
# Protege contra desalinhamento se algum byte se perder na porta serial:
# o parser procura ativamente o sync e resincroniza sozinho se o
# checksum não bater, em vez de ficar lendo lixo pra sempre.
SYNC_BYTES = b'\xAA\x55'
FRAME_SIZE = len(SYNC_BYTES) + STRUCT_SIZE + 1

CSV_PATH = "telemetria_guardada.csv"
MBTILES_PATH = "Falcon6_Mapa_UNIFEI.mbtiles"
COLUNAS = ["Hora_Recebimento"] + FIELD_NAMES


def calcular_checksum(payload: bytes) -> int:
    chk = 0
    for b in payload:
        chk ^= b
    return chk


def extrair_pacotes(buffer: bytearray):
    """Varre o buffer procurando quadros válidos (sync + payload + checksum).
    Retorna (lista_de_payloads_validos, buffer_restante)."""
    pacotes = []
    while True:
        idx = buffer.find(SYNC_BYTES)
        if idx == -1:
            # sem sync no buffer: descarta tudo, exceto um possível sync
            # partido no finalzinho (aguarda o próximo byte pra confirmar)
            if len(buffer) > 0 and buffer[-1:] == SYNC_BYTES[:1]:
                del buffer[:-1]
            else:
                buffer.clear()
            break
        del buffer[:idx]  # descarta lixo antes do sync
        if len(buffer) < FRAME_SIZE:
            break  # quadro incompleto, espera mais bytes chegarem
        payload = bytes(buffer[len(SYNC_BYTES):len(SYNC_BYTES) + STRUCT_SIZE])
        checksum_recebido = buffer[len(SYNC_BYTES) + STRUCT_SIZE]
        if calcular_checksum(payload) == checksum_recebido:
            pacotes.append(payload)
            del buffer[:FRAME_SIZE]
        else:
            # sync falso ou corrupção: descarta só o sync e tenta de novo
            del buffer[:len(SYNC_BYTES)]
    return pacotes, buffer


def get_mbtiles_bounds(mbtiles_path):
    try:
        conn = sqlite3.connect(mbtiles_path)
        cursor = conn.cursor()
        cursor.execute("SELECT value FROM metadata WHERE name='bounds'")
        row = cursor.fetchone()
        conn.close()
        if row:
            bounds = [float(x) for x in row[0].split(',')]
            return [[bounds[1], bounds[0]], [bounds[3], bounds[2]]]
    except Exception:
        pass
    return [[-19.679, -43.215], [-19.674, -43.209]]


def salvar_registro_telemetria(novo_registro):
    df_novo = pd.DataFrame([novo_registro])
    hdr = not os.path.exists(CSV_PATH)
    df_novo.to_csv(CSV_PATH, mode='a', header=hdr, index=False)


def carregar_dados():
    if os.path.exists(CSV_PATH):
        try:
            df = pd.read_csv(CSV_PATH)
            if not df.empty:
                return df
        except Exception:
            pass
    return pd.DataFrame(columns=COLUNAS)


def obter_sessao_atual(df):
    if df.empty:
        return df
    resets = df.index[df['Pacote_ID'].diff() < 0].tolist()
    if resets:
        return df.iloc[resets[-1]:]
    return df


telemetria_df = obter_sessao_atual(carregar_dados())

# --- BARRA LATERAL ---
st.sidebar.header("⚙️ Configurações do Dashboard")
auto_refresh = st.sidebar.toggle("🔄 Atualização em Tempo Real", value=True)
intervalo_refresh = st.sidebar.slider("Intervalo de Atualização (segundos)", 0.2, 3.0, 0.5, step=0.1)
reset_graficos = st.sidebar.button("🎯 Resetar Zoom de Todos os Gráficos")

st.sidebar.markdown("---")
st.sidebar.header("🔌 Conexão Serial (USB)")
portas_disponiveis = [p.device for p in serial.tools.list_ports.comports()] or ["Nenhuma porta encontrada"]
porta_selecionada = st.sidebar.selectbox("Selecione a Porta COM do ESP32 (Receptor):", portas_disponiveis)
baud_rate = st.sidebar.selectbox("Baud Rate:", [115200, 9600], index=0)
conectar = st.sidebar.checkbox("Conectar ao ESP32")

st.sidebar.markdown("---")
st.sidebar.caption(f"Pacote LoRa esperado: **{STRUCT_SIZE} bytes** de payload "
                    f"({len(FIELDS)} campos), quadro USB de **{FRAME_SIZE} bytes** "
                    f"com sync + checksum.")

# --- CONEXÃO E LEITURA DA SERIAL (com framing) ---
ser = None
if conectar and porta_selecionada != "Nenhuma porta encontrada":
    try:
        if "ser" not in st.session_state or not st.session_state.ser.is_open:
            st.session_state.ser = serial.Serial(porta_selecionada, baud_rate, timeout=1)
            st.sidebar.success(f"Conectado em {porta_selecionada}")
        ser = st.session_state.ser
    except Exception as e:
        st.sidebar.error(f"Erro ao conectar: {e}")

if "serial_buffer" not in st.session_state:
    st.session_state.serial_buffer = bytearray()

if conectar and ser and ser.is_open:
    if ser.in_waiting > 0:
        st.session_state.serial_buffer.extend(ser.read(ser.in_waiting))

    pacotes, st.session_state.serial_buffer = extrair_pacotes(st.session_state.serial_buffer)

    for raw_payload in pacotes:
        try:
            unpacked = struct.unpack(STRUCT_FORMAT, raw_payload)
            hora_atual = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            novo_registro = dict(zip(FIELD_NAMES, unpacked))
            novo_registro["Hora_Recebimento"] = hora_atual
            salvar_registro_telemetria(novo_registro)
        except struct.error:
            continue

    if pacotes:
        telemetria_df = obter_sessao_atual(carregar_dados())

# --- MÉTRICAS PRINCIPAIS ---
col0, col1, col2, col3, col4, col5, col6 = st.columns(7)

pacote_id = 0
alt_atual = 0.0
alt_max = 0.0
sats = 0
vel_atual = 0.0
fix_valido = "—"
status_squib_str = "AGUARDANDO"

if not telemetria_df.empty:
    ultimo_dado = telemetria_df.iloc[-1]
    pacote_id = int(ultimo_dado["Pacote_ID"])
    alt_atual = float(ultimo_dado["Alt_BMP"])
    alt_max = float(ultimo_dado["Alt_Max"])
    sats = int(ultimo_dado["Satélites"])
    vel_atual = float(ultimo_dado["Velocidade_GPS"])
    fix_valido = "✅ Sim" if int(ultimo_dado["Validade_Localizacao"]) == 1 else "❌ Não"

    sq_code = int(ultimo_dado["Status_Squib"])
    if sq_code == 1:
        status_squib_str = "🔥 DISPARANDO"
    elif sq_code == 2:
        status_squib_str = "✅ DISPARADO"
    else:
        status_squib_str = "⏳ AGUARDANDO"

col0.metric("ID Pacote", f"{pacote_id}")
col1.metric("Altitude Atual", f"{alt_atual:.2f} m")
col2.metric("Altitude Máxima", f"{alt_max:.2f} m")
col3.metric("Velocidade (GPS)", f"{vel_atual:.2f} m/s")
col4.metric("Fix GPS Válido", fix_valido)
col5.metric("Satélites GPS", f"{sats}")
col6.metric("Status Squib", status_squib_str)

st.markdown("---")

col_mapa, col_3d = st.columns(2)

with col_mapa:
    st.subheader("MAPA - Trajetória GPS (Local)")
    bounds = get_mbtiles_bounds(MBTILES_PATH)
    lat_center = (bounds[0][0] + bounds[1][0]) / 2
    lon_center = (bounds[0][1] + bounds[1][1]) / 2

    if not telemetria_df.empty:
        lat_center = float(telemetria_df.iloc[-1]["Latitude"])
        lon_center = float(telemetria_df.iloc[-1]["Longitude"])

    m = folium.Map(location=[lat_center, lon_center], zoom_start=15)

    if not telemetria_df.empty:
        coords = telemetria_df[["Latitude", "Longitude"]].values.tolist()
        folium.PolyLine(coords, color="red", weight=4, opacity=0.8).add_to(m)
        folium.Marker(
            coords[-1],
            popup=f"Foguete: {alt_atual:.1f}m",
            icon=folium.Icon(color="red", icon="rocket", prefix="fa")
        ).add_to(m)

    st_folium(m, width=600, height=350, key=f"mapa_gps_{len(telemetria_df)}", returned_objects=[])

with col_3d:
    st.subheader("🚀 Trajetória 3D")
    if not telemetria_df.empty:
        fig_3d = go.Figure(data=[go.Scatter3d(
            x=telemetria_df['Longitude'],
            y=telemetria_df['Latitude'],
            z=telemetria_df['Alt_BMP'],
            mode='lines+markers',
            marker=dict(size=3, color=telemetria_df['Alt_BMP'], colorscale='Viridis'),
            line=dict(color='red', width=4)
        )])
        fig_3d.update_layout(
            margin=dict(l=0, r=0, b=0, t=0),
            scene=dict(xaxis_title='Longitude', yaxis_title='Latitude', zaxis_title='Altitude (m)'),
            height=350
        )
        st.plotly_chart(fig_3d, use_container_width=True, key=f"plot_3d_{len(telemetria_df)}")
    else:
        st.info("Aguardando pacotes de dados para gerar renderização 3D...")

st.markdown("---")

col_g1, col_g2 = st.columns(2)
key_suffix = f"_{int(time.time())}" if reset_graficos else ""

with col_g1:
    st.subheader("📈 Altitude vs Tempo")
    if not telemetria_df.empty:
        fig_alt = px.line(telemetria_df, x="Tempo_ms", y=["Alt_BMP", "Alt_Max"],
                           labels={"value": "Altitude (m)", "Tempo_ms": "Tempo (ms)"})
        fig_alt.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10),
                               xaxis=dict(rangemode="tozero"), yaxis=dict(rangemode="tozero"))
        st.plotly_chart(fig_alt, use_container_width=True, key=f"fig_alt{key_suffix}")
    else:
        st.info("Aguardando dados...")

with col_g2:
    st.subheader("⚡ Velocidade (GPS) vs Tempo")
    if not telemetria_df.empty:
        fig_vel = px.line(telemetria_df, x="Tempo_ms", y="Velocidade_GPS",
                           labels={"Velocidade_GPS": "Velocidade (m/s)", "Tempo_ms": "Tempo (ms)"})
        fig_vel.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10),
                               xaxis=dict(rangemode="tozero"), yaxis=dict(rangemode="tozero"))
        st.plotly_chart(fig_vel, use_container_width=True, key=f"fig_vel{key_suffix}")
    else:
        st.info("Aguardando dados...")

col_g3, col_g4 = st.columns(2)

with col_g3:
    st.subheader("🧭 Orientação (Roll / Pitch / Yaw) — BNO055")
    if not telemetria_df.empty:
        fig_ori = px.line(telemetria_df, x="Tempo_ms", y=["Roll", "Pitch", "Yaw"],
                           labels={"value": "Graus (°)", "Tempo_ms": "Tempo (ms)"})
        fig_ori.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10))
        st.plotly_chart(fig_ori, use_container_width=True, key=f"fig_ori{key_suffix}")
    else:
        st.info("Aguardando dados...")

with col_g4:
    st.subheader("📉 Aceleração Linear — BNO055")
    if not telemetria_df.empty:
        fig_acc = px.line(telemetria_df, x="Tempo_ms", y=["Acc_Lin_X", "Acc_Lin_Y", "Acc_Lin_Z"],
                           labels={"value": "Aceleração (m/s²)", "Tempo_ms": "Tempo (ms)"})
        fig_acc.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10), xaxis=dict(rangemode="tozero"))
        st.plotly_chart(fig_acc, use_container_width=True, key=f"fig_acc{key_suffix}")
    else:
        st.info("Aguardando dados...")

st.markdown("---")

st.subheader("📋 Histórico Geral de Pacotes Recebidos (via LoRa)")
if not telemetria_df.empty:
    st.dataframe(telemetria_df.iloc[::-1], use_container_width=True)
else:
    st.info("Nenhum pacote recebido até o momento.")

if not telemetria_df.empty:
    st.download_button(
        label="📦 Baixar Telemetria (LoRa, ao vivo)",
        data=telemetria_df.to_csv(index=False).encode('utf-8'),
        file_name=f"telemetria_lora_{int(time.time())}.csv",
        mime="text/csv",
    )

st.markdown("---")
if st.button("🗑️ Limpar Histórico Salvo (LoRa)"):
    if os.path.exists(CSV_PATH):
        os.remove(CSV_PATH)
    st.session_state.serial_buffer = bytearray()
    st.rerun()

# =============================================================================
# --- ANÁLISE PÓS-VOO: dados completos do cartão SD (47 colunas) ---
# Esses sensores (accel bruta/linear, giroscópio, magnetômetro, orientação
# completa) não são mais transmitidos pelo LoRa (pra manter o pacote pequeno
# e o link confiável). Eles continuam gravados no SD a 10Hz; suba o
# voo_telemetria.csv aqui depois do voo pra ver esses gráficos.
# =============================================================================
st.markdown("---")
st.header("🗂️ Análise Pós-Voo (dados completos do Cartão SD)")
st.caption("Suba o arquivo voo_telemetria.csv gravado no SD para ver aceleração, "
           "giroscópio, magnetômetro e orientação completos — dados que não vêm pelo rádio.")

sd_file = st.file_uploader("Selecione o voo_telemetria.csv do SD", type="csv")

if sd_file is not None:
    try:
        df_sd = pd.read_csv(sd_file)
        st.success(f"{len(df_sd)} registros carregados do SD.")

        col_sd1, col_sd2 = st.columns(2)
        with col_sd1:
            st.subheader("🧭 Orientação (Roll / Pitch / Yaw)")
            cols_ori = [c for c in ["Roll_deg", "Pitch_deg", "Yaw_Heading_deg"] if c in df_sd.columns]
            if cols_ori:
                fig = px.line(df_sd, x="Tempo_ms", y=cols_ori, labels={"value": "Graus (°)"})
                fig.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10))
                st.plotly_chart(fig, use_container_width=True, key="sd_orientacao")

        with col_sd2:
            st.subheader("🌐 Giroscópio")
            cols_gyr = [c for c in ["Vel_Ang_x_rad/s", "Vel_Ang_y_rad/s", "Vel_Ang_z_rad/s"] if c in df_sd.columns]
            if cols_gyr:
                fig = px.line(df_sd, x="Tempo_ms", y=cols_gyr, labels={"value": "rad/s"})
                fig.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10))
                st.plotly_chart(fig, use_container_width=True, key="sd_giroscopio")

        col_sd3, col_sd4 = st.columns(2)
        with col_sd3:
            st.subheader("📉 Aceleração Linear")
            cols_accl = [c for c in ["Acc_L_x_m/s2", "Acc_L_y_m/s2", "Acc_L_z_m/s2"] if c in df_sd.columns]
            if cols_accl:
                fig = px.line(df_sd, x="Tempo_ms", y=cols_accl, labels={"value": "m/s²"})
                fig.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10))
                st.plotly_chart(fig, use_container_width=True, key="sd_accel_linear")

        with col_sd4:
            st.subheader("🧲 Magnetômetro")
            cols_mag = [c for c in ["Mag_x_uT", "Mag_y_uT", "Mag_z_uT"] if c in df_sd.columns]
            if cols_mag:
                fig = px.line(df_sd, x="Tempo_ms", y=cols_mag, labels={"value": "µT"})
                fig.update_layout(height=320, margin=dict(l=10, r=10, t=20, b=10))
                st.plotly_chart(fig, use_container_width=True, key="sd_mag")

        st.subheader("📋 Dados Completos do SD")
        st.dataframe(df_sd.iloc[::-1], use_container_width=True)
    except Exception as e:
        st.error(f"Erro ao ler o CSV do SD: {e}")

if auto_refresh:
    time.sleep(intervalo_refresh)
    st.rerun()
