#!/usr/bin/env python3
"""Render the production LVGL UI on Linux (gcc, g++, ninja; Pillow for PNG)."""
from pathlib import Path
import os, sys, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);source=ROOT/'firmware';include=source/'include';lvgl=source/'components/lvgl'
 common=f'-O1 -I{ROOT}/tests/ui_stubs -I{include} -I{lvgl} -I{lvgl}/src -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE'
 files=list((lvgl/'src').rglob('*.c'))+list((source/'src').glob('gluco_font_*.c'))+[source/'src'/x for x in ['ui.cpp','fonts.cpp','weather_icons.cpp']]+[ROOT/'tests/ui_render.cpp']
 from concurrent.futures import ThreadPoolExecutor
 def compile_file(item):
  path, obj = item
  compiler = 'gcc' if path.suffix == '.c' else 'g++'
  std = '-std=c11' if path.suffix == '.c' else '-std=c++17'
  cmd = [compiler, *common.split(), std, '-c', str(path), '-o', str(obj)]
  res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
  if res.returncode != 0:
   print(f"Error compiling {path}:\n{res.stderr}")
   sys.exit(1)

 tasks = []
 objects = []
 for i, path in enumerate(files):
  obj = d / f'{i}.o'
  objects.append(obj)
  tasks.append((path, obj))

 with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
  list(pool.map(compile_file, tasks))

 target = d / 'render'
 link_cmd = ['g++', *[str(o) for o in objects], '-lm', '-o', str(target)]
 res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
 if res.returncode != 0:
  print(f"Error linking render:\n{res.stderr}")
  sys.exit(1)
 (ROOT/'docs').mkdir(exist_ok=True)
 from PIL import Image
 for name,args in [('pantalla',[]),('ajustes',['setup'])]:
  ppm=d/(name+'.ppm');subprocess.run([str(target),str(ppm),*args],check=True)
  Image.open(ppm).save(ROOT/'docs'/(name+'.png'))
 print('OK: production UI rendered at 800x480')
