### v 2022.3
- reverted to non-SSL because SSL was too slow for ESP8266 even with a 160 Mhz clock

### v 2022.2
- use SSL (https) for api endpoints. self signed certificate provided and generation script
- use mDNS responder
- improved button handling
- faster and more reliable tare function
- button skips wifi connection if pressed in the first 3 seconds
- changed UI to reflect all that
- moved config into it's own file
- moved structures into a header file
- turn off wifi, display and enter deep sleep when doing a soft power off. In case you don't build the power-on latching circuitry this might come handy
- added example
