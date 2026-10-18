#!/usr/bin/env python3
import sys
import re

LOG_FILE = "/home/rober/gluco_waveshare/monitor_30m.log"

def analyze():
    try:
        with open(LOG_FILE) as f:
            lines = f.readlines()
    except Exception as e:
        print(f"No se pudo leer {LOG_FILE}: {e}")
        return

    libre_readings = []
    dm_uploads = []
    weather_queries = []
    health_reports = []
    errors = []

    time_re = re.compile(r"^\[(\d{2}:\d{2}:\d{2})\]")

    last_time = None
    for line in lines:
        line = line.strip()
        m = time_re.match(line)
        t_str = m.group(1) if m else None

        if "Consultando glucosa LibreLinkUp" in line:
            libre_readings.append({"time": t_str, "status": "start"})
        elif "HTTPS 200" in line and "bytes" in line and "ms" in line:
            ms_match = re.search(r"(\d+)\s+ms", line)
            bytes_match = re.search(r"(\d+)\s+bytes", line)
            ms = int(ms_match.group(1)) if ms_match else 0
            b = int(bytes_match.group(1)) if bytes_match else 0
            if "api-eu.libreview.io" in line or b > 5000:
                libre_readings.append({"time": t_str, "ms": ms, "bytes": b, "status": "ok"})
            elif "diabetes-m.com" in line or b < 4000:
                dm_uploads.append({"time": t_str, "ms": ms, "bytes": b, "status": "ok"})
        elif "Lectura sincronizada correctamente:" in line:
            g_match = re.search(r"(\d+)\s+mg/dL", line)
            if g_match:
                dm_uploads.append({"time": t_str, "glucose": int(g_match.group(1))})
        elif "TIR=" in line:
            tir_match = re.search(r"TIR=([\d\.]+)%", line)
            avg_match = re.search(r"media=(\d+)", line)
            samples_match = re.search(r"muestras=(\d+)", line)
            if tir_match:
                libre_readings.append({
                    "time": t_str,
                    "tir": float(tir_match.group(1)),
                    "avg": int(avg_match.group(1)) if avg_match else 0,
                    "samples": int(samples_match.group(1)) if samples_match else 0
                })
        elif "Consultando clima Open-Meteo" in line:
            weather_queries.append({"time": t_str})
        elif "[HEALTH]" in line:
            uptime_m = re.search(r"uptime=(\d+)", line)
            int_m = re.search(r"internal=(\d+)", line)
            largest_m = re.search(r"largest=(\d+)", line)
            psram_m = re.search(r"psram=(\d+)", line)
            if uptime_m and int_m:
                health_reports.append({
                    "time": t_str,
                    "uptime": int(uptime_m.group(1)),
                    "internal": int(int_m.group(1)),
                    "largest": int(largest_m.group(1)),
                    "psram": int(psram_m.group(1))
                })
        elif "Error" in line or "timeout" in line.lower() or "fallo" in line.lower():
            errors.append({"time": t_str, "line": line})

    print(f"=== REPORTE DE ANÁLISIS DE MONITORIZACIÓN ===")
    print(f"Total líneas registradas: {len(lines)}")
    print(f"Reportes [HEALTH]: {len(health_reports)}")
    if health_reports:
        first_h = health_reports[0]
        last_h = health_reports[-1]
        print(f"  Memoria inicial: Internal={first_h['internal']} B, Largest={first_h['largest']} B, PSRAM={first_h['psram']} B")
        print(f"  Memoria final:   Internal={last_h['internal']} B, Largest={last_h['largest']} B, PSRAM={last_h['psram']} B")
        diff_int = last_h['internal'] - first_h['internal']
        print(f"  Diferencia heap: {diff_int:+d} B (estabilidad de memoria: {'EXCELENTE' if abs(diff_int) < 2048 else 'REVISAR'})")

    print(f"Consultas meteorológicas: {len(weather_queries)}")
    print(f"Fallos o reintentos registrados: {len(errors)}")
    for e in errors:
        print(f"  [{e['time']}] {e['line']}")

if __name__ == "__main__":
    analyze()
