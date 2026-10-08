# SPDX-License-Identifier: 0BSD
"""Reset an explicitly selected lab board and capture a bounded serial log."""
import argparse
import pathlib
import time
import serial

p=argparse.ArgumentParser()
p.add_argument('port')
p.add_argument('output',type=pathlib.Path)
p.add_argument('--seconds',type=int,default=10)
p.add_argument('--no-reset',action='store_true')
a=p.parse_args()
a.output.parent.mkdir(parents=True,exist_ok=True)
with serial.Serial(a.port,115200,timeout=.2) as s, a.output.open('wb') as output:
    if not a.no_reset:
        s.dtr=False
        s.rts=True
        time.sleep(.15)
        s.rts=False
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        chunk=s.read(s.in_waiting or 1)
        if chunk: output.write(chunk); output.flush()
print(a.output)
