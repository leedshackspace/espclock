# Leeds hackspace ESP8266 clock

Controller code for the large wallmounted LED clock.

Each digit has a dedicated 8-bit shift register and drive electronics.

A pair of linear regulators supply 3.3v for the esp8266 and panel driver.

A boost converted generates ~30v for the LEDs (Each segment is 3 strings of 15 LEDs).

Ideal supply voltage is ~20v. It will work down to 12v, but the boost converter tends to get a bit warm at lower voltages.

Pressing the button within 5 seconds of poweron will put the clock into setup mode.
This provides an access point and web page to configure wifi and NTP settings.
