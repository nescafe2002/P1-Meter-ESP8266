# P1-Meter-ESP8266
Software for the ESP2866 that sends P1 smart meter data to Emoncms (with CRC checking and OTA firmware updates)

### Installation instrucions
- Make sure that your ESP8266 can be flashed from the Arduino environnment: https://github.com/esp8266/Arduino
- Install the SoftSerial library from: https://github.com/plerup/espsoftwareserial
- Place all files from this repository in a directory. Open the .ino file.
- Adjust WIFI, Emoncms and debug settings at the top of the file
- Compile and flash

### Connection of the P1 meter to the ESP8266
You need to connect the smart meter with a RJ11 connector. This is the pinout to use
![RJ11 P1 connetor](http://gejanssen.com/howto/Slimme-meter-uitlezen/RJ11-pinout.png)

Connect GND->GND on ESP, RTS->3.3V on ESP and RxD->any digital pin on ESP. In this sketch I use D5

For Sagemcom T210-D a 10K resistor should be placed between RTS and RxD (ref. [issue #6](/jantenhove/P1-Meter-ESP8266/issues/6)).

### Changelog
- Added support for three phase system (L1/L2/L3)
- Added support for Sagemcom T210-D (softserial read in single command, update once every 10 seconds)
- Improved OTA support (disable softserial rx when updating)
- Added support for Emoncms
- Added udp logger SendToDebug()
