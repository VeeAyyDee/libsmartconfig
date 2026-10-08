# SPDX-License-Identifier: 0BSD
"""Run an isolated-lab UI test and capture the receiver's serial evidence."""
import argparse
import json
import pathlib
import subprocess
import threading
import time
import serial

p=argparse.ArgumentParser()
p.add_argument('--adb',required=True)
p.add_argument('--port',required=True)
p.add_argument('--name',required=True)
p.add_argument('--protocol',type=int,default=0)
p.add_argument('--mode',type=int,default=0)
p.add_argument('--scenario',default='success')
p.add_argument('--bssid',default='80:65:99:df:56:81')
p.add_argument('--capture',action='store_true')
a=p.parse_args()
out=pathlib.Path(__file__).resolve().parents[1]/'validation'
out.mkdir(exist_ok=True)
stop=threading.Event()
with serial.Serial(a.port,115200,timeout=.2) as uart:
    uart.dtr=False; uart.rts=True; time.sleep(.15); uart.rts=False
    def capture():
        with (out/(a.name+'-receiver.txt')).open('wb') as f:
            while not stop.is_set():
                b=uart.read(uart.in_waiting or 1)
                if b: f.write(b); f.flush()
    reader=threading.Thread(target=capture); reader.start()
    started=time.monotonic()
    try:
        process=subprocess.Popen([a.adb,'shell','am','instrument','-w','-e','protocol',str(a.protocol),
            '-e','mode',str(a.mode),'-e','scenario',a.scenario,'-e','bssid',a.bssid,'-e','capture',str(a.capture).lower(),
            'dev.libsmartconfig.testapp.test/dev.libsmartconfig.testapp.LabInstrumentation'],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        # MIUI can defer an instrumentation process's background Activity launch.
        # An explicit shell launch brings the same test Activity to the foreground.
        time.sleep(2)
        activity_state=subprocess.run([a.adb,'shell','dumpsys','activity','activities'],capture_output=True,text=True).stdout
        resumed=[line for line in activity_state.splitlines() if 'mResumedActivity' in line]
        if not any('dev.libsmartconfig.testapp/.MainActivity' in line for line in resumed):
            subprocess.run([a.adb,'shell','am','start','-f','0x20000000','-n','dev.libsmartconfig.testapp/.MainActivity'],capture_output=True)
        try:
            stdout,stderr=process.communicate(timeout=145)
        except subprocess.TimeoutExpired:
            subprocess.run([a.adb,'shell','am','force-stop','dev.libsmartconfig.testapp'],capture_output=True)
            process.kill(); process.communicate()
            raise
        text=stdout+stderr
        (out/(a.name+'-phone.txt')).write_text(text,encoding='utf-8')
        print(text,flush=True)
        elapsed=round(time.monotonic()-started,2)
        time.sleep(3.5) # Receiver sends repeated replies after the phone accepts the first one.
        (out/(a.name+'.json')).write_text(json.dumps(dict(protocol=a.protocol,mode=a.mode,
            scenario=a.scenario,passed='PASS '+a.scenario in text,elapsed_seconds=elapsed),indent=2))
        if a.capture:
            for position in ['top','bottom']:
                with (out/('preview-'+position+'.png')).open('wb') as image:
                    subprocess.run([a.adb,'exec-out','run-as','dev.libsmartconfig.testapp','cat','cache/preview-'+position+'.png'],stdout=image,check=True)
                subprocess.run([a.adb,'shell','run-as','dev.libsmartconfig.testapp','rm','cache/preview-'+position+'.png'],check=True)
    finally:
        stop.set(); reader.join()
        if a.scenario=='wifi-loss':
            subprocess.run([a.adb,'shell','cmd','wifi','set-wifi-enabled','enabled'],capture_output=True)
            time.sleep(2)
            subprocess.run([a.adb,'shell','cmd','wifi','connect-network','SmartConfig-Lab','wpa2','LabPass123','-b',a.bssid],capture_output=True)
        subprocess.run([a.adb,'shell','am','start','-n','dev.libsmartconfig.testapp/.MainActivity'],capture_output=True)
if 'PASS '+a.scenario not in text:
    raise SystemExit(1)
