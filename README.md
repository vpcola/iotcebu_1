# MQTT Secure via HTTPS Websocket Example

Uses an mbedTLS socket to make a very simple HTTPS request over a secure connection, including verifying the server TLS certificate.
This setup uses the secure MQTT broker at mqtt.iotcebu.com by default, if you need to setup your own secure MQTT broker,
do a make menuconfig to change the default settings. Don't forget to change the certificate file cert.c with your own
server.

## Example MQTT
* Periodically publishes temperature and humidity data to MQTT and sleep 
* IO 4 is connected to an LED (to indicate the MCU is active)
* IO 16 is connected to the out pin of the DHT22 (Temperature and Humidity) sensor

## esp-idf used
* commit fd3ef4cdfe1ce747ef4757205f4bb797b099c9d9
* Merge: 94a61389 52c378b4
* Author: Angus Gratton <angus@espressif.com>
* Date:   Fri Apr 21 12:27:32 2017 +0800

## License
* Apache 2.0

## eclipse
* include include.xml to C-Paths

## PREPARE
1. change main/cert.c -> server_root_cert
2. do a "make menuconfig" to change the default settings

(Hint: today mbed_ssl without WebSockets is unstable, i.e. reconnecting is needed some times, searching for reasons...)

## INSTALL
* `make menuconfig`
* `make -j8 all`
* `make flash`
* `make monitor`




