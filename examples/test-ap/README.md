# Isolated SmartConfig test access point

This ESP32-S3 app creates WPA2 network `SmartConfig-Lab` with public test password
`LabPass123`, channel 6 and default DHCP subnet `192.168.4.0/24`. It has no internet
uplink. It is intended for a phone and a second S3 running `examples/provision`.

In an activated ESP-IDF environment:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p YOUR_AP_BOARD_PORT flash monitor
```

Built and booted with ESP-IDF 5.5.2 in the Android sender lab. The AP prints its
BSSID on boot. Use that BSSID in the Android sender. Keep the phone on this
network when Android warns that internet is unavailable. Do not use these
public credentials for a production network.

Wi-Fi configuration stays in RAM; NVS remains enabled for PHY calibration.
The program does not erase NVS on initialization errors. Flash only a designated
test board. See `android/VALIDATION.md` for the phone provisioning evidence.
