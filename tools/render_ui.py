#!/usr/bin/env python3
"""Render the production LVGL UI on Linux (gcc, g++, ninja; Pillow for PNG)."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);source=ROOT/'firmware';include=source/'include';lvgl=source/'components/lvgl'
 common=f'-O1 -I{ROOT}/tests/ui_stubs -I{include} -I{lvgl} -I{lvgl}/src -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE'
 files=list((lvgl/'src').rglob('*.c'))+list((source/'src').glob('gluco_font_*.c'))+[source/'src'/x for x in ['ui.cpp','fonts.cpp','weather_icons.cpp']]+[ROOT/'tests/ui_render.cpp']
 lines=['rule cc','  command = $compiler $flags -c $in -o $out','rule link','  command = g++ $in -lm -o $out']
 objects=[]
 for i,path in enumerate(files):
  obj=d/f'{i}.o';objects.append(obj)
  lines.extend([f'build {obj}: cc {path}',f'  compiler = '+('gcc' if path.suffix=='.c' else 'g++'),f'  flags = {common} '+('-std=c11' if path.suffix=='.c' else '-std=c++17')])
 target=d/'render';lines.append(f'build {target}: link '+ ' '.join(map(str,objects)))
 (d/'build.ninja').write_text('\n'.join(lines)+'\n')
 result=subprocess.run(['ninja','-C',str(d),'-j','8'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if result.returncode:
  print(result.stdout)
  result.check_returncode()
 (ROOT/'docs').mkdir(exist_ok=True)
 from PIL import Image
 for name,args in [('pantalla',[]),('ajustes',['setup'])]:
  ppm=d/(name+'.ppm');subprocess.run([str(target),str(ppm),*args],check=True)
  Image.open(ppm).save(ROOT/'docs'/(name+'.png'))
 print('OK: production UI rendered at 800x480')
