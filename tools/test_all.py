#!/usr/bin/env python3
import argparse, pathlib, subprocess, tempfile, datetime
ROOT=pathlib.Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser();a.add_argument('--sanitize',action='store_true');opts=a.parse_args()
with tempfile.TemporaryDirectory() as d:
 flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if opts.sanitize else []
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I'+str(ROOT/'firmware/include'),str(ROOT/'tests/core_test.cpp'),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
 subprocess.run(['g++','-std=c++17',*flags,'-DARDUINOJSON_ENABLE_ARDUINO_STRING=1','-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(ROOT/'tests/ui_stubs'),'-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/components/lvgl'),'-I'+str(ROOT/'firmware/components/ArduinoJson/src'),str(ROOT/'tests/providers_test.cpp'),str(ROOT/'firmware/src/providers.cpp'),'-o',d+'/providers'],check=True)
 subprocess.run([d+'/providers'],check=True)
subprocess.run(['node','--check',str(ROOT/'web/app.js')],check=True)
subprocess.run(['python3',str(ROOT/'tests/install_test.py')],check=True)
print('OK: history/units/UTC/rollover, 1000 corrupt histories, JavaScript syntax')
