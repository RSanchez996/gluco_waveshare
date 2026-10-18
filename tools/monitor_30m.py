#!/usr/bin/env python3
import os
import sys
import time
import re
from datetime import datetime

PORT = "/dev/ttyACM0"
DURATION = 1800  # 30 minutes
LOG_FILE = "/home/rober/gluco_waveshare/monitor_30m.log"

print(f"[{datetime.now().strftime('%H:%M:%S')}] Iniciando monitorización de 30 minutos en {PORT}...")

fd = os.open(PORT, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)

start_time = time.time()
end_time = start_time + DURATION

buffer = ""
with open(LOG_FILE, "w", buffering=1) as log:
    log.write(f"=== INICIO MONITORIZACIÓN {datetime.now().isoformat()} ===\n")
    while time.time() < end_time:
        try:
            chunk = os.read(fd, 4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                buffer += text
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if line:
                        now_str = datetime.now().strftime("%H:%M:%S")
                        log.write(f"[{now_str}] {line}\n")
                        print(f"[{now_str}] {line}", flush=True)
        except BlockingIOError:
            pass
        except Exception as e:
            print(f"Error leyendo puerto: {e}", flush=True)
        time.sleep(0.05)
    log.write(f"=== FIN MONITORIZACIÓN {datetime.now().isoformat()} ===\n")

os.close(fd)
print(f"[{datetime.now().strftime('%H:%M:%S')}] Monitorización de 30 minutos completada. Analizando métricas...")
