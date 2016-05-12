SKETCH = esp8266-ledclock.ino

LIBS ?= $(ESP_LIBS)/Wire \
        $(ESP_LIBS)/EEPROM \
        $(ESP_LIBS)/Ticker \
        Time \
        $(ESP_LIBS)/ESP8266WiFi \
        $(ESP_LIBS)/ESP8266mDNS \
        $(ESP_LIBS)/ESP8266WebServer \
        $(ESP_LIBS)/ESP8266HTTPClient

include ../makeEspArduino/makeEspArduino.mk
