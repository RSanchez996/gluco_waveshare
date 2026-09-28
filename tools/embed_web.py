#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parts=['// Generated from web/. Do not edit.\n#pragma once\n#include <pgmspace.h>\n']
for key,name in [('INDEX','index.html'),('CSS','style.css'),('JS','app.js')]:
    data=(root/'web'/name).read_text(encoding='utf-8');delimiter='GLUCO_'+key
    if ')'+delimiter+'"' in data:raise SystemExit('raw string delimiter collision')
    parts.append(f'const char WEB_{key}[] PROGMEM = R"{delimiter}({data}){delimiter}";\n')
(root/'firmware'/'include'/'web_assets.hpp').write_text(''.join(parts),encoding='utf-8')
print('web_assets.hpp generated')
