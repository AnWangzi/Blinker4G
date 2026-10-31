"""Compile and run real library code against deterministic fake serial/time/EEPROM."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--arduinojson', required=True, help='ArduinoJson 6 library root')
parser.add_argument('--cxx', default='g++')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
out = root / '.build' / 'native'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'regression.exe'
subprocess.run([
    args.cxx, '-std=c++11', '-Wall', '-Wextra', '-DESP8266',
    '-I' + str(root / 'tests' / 'fakes'), '-I' + str(root / 'src'),
    '-I' + str(Path(args.arduinojson).resolve() / 'src'),
    str(root / 'tests' / 'regression.cpp'),
    str(root / 'src' / 'Blinker4G.cpp'),
    str(root / 'src' / 'Blinker4GWhLte7S0Driver.cpp'),
    '-o', str(binary)
], check=True, timeout=60)
subprocess.run([str(binary)], check=True, timeout=60)
