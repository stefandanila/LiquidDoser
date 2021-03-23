# LiquidDoser
This is made with ESP8266 ESP01. The ESP8266 ESP01 act as a http server and as a routines manager. The doser can command 4 DC perisaltic pumps. The UI is made with AngularJs and Boostrap, the communication between fron-end and back-end is made with RestAPI calls. The configuration is stored in files on ESP8266 ESP01.

## Hardware parts
The listed hardware parts should be connected as in CircuitSchematics.png, an working example can be seen in HarwareExample.jpg. Warning, working with high voltage without proper knowlege and protection equipment can kill you.
- 1 x ESP8266 ESP01
- 1 x Power supply 12V 1Amp
- 1 x AMS1117 3.3V 5V DC-DC Step-Down power supply module
- 1 x 4-Channels Relay Module, 5V
- 1 x MP1584EN (this should be reglated to 5v output)
- 4 x 12V DC Perisaltic pumps
- Wires

## Software setup
To install the sketches use Arduino IDE, install the ESP board in Settings -> Additional Boards Manager URLs. The sketches make use of the following libraries: NTPClient (https://github.com/arduino-libraries/NTPClient).

## Configuring the network setting
### From the code
Make changes in esp8266/data/network.config to suit your network configuration 
### From UI
 A network settings page is available if the file esp8266/data/network.config is not uploaded to esp or if it is empty. Use your smart phone and make a hostspot with ssid: config and password: 12345678 , then monitor the hostspot for connected devices copy the ip address, and use a browser from same smart phone to connect to that ip.

# Disclaimer
This project is something I put togher to control and monitor my personal aquarium ferts dosage, it is my second project with esp, I am sure that lots of thing can be done better and I am ager to learn them from your input. This project can be use as it is or modify it to suit your need, for me it is working since winter of 2020.

