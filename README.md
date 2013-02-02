WaterMote
=========

Code used in WattMote, a low cost wireless pulse counter reader (eg. water meter)
Load this sketch on your Arduino/Moteino to read a SY310 photo reflective sensor and transmit the data to a gateway Moteino that passes it along to the host (RaspberryPi, PC, etc).

It will send a message of this format to the gateway:

GPM:1.23 GAL:1234.56 GLM:1.23

GPM=Gallons per minute (realtime water flow)
GLM=Gallons last minute (gallons used last minute)
GAL=Gallons used since beginning of time

It also pulses a onboard LED to signal activity:
- every pulse detected will flip the LED state
- if there's no pulse activity it will flip the LED state every 5 seconds

Records the GAL counter as a LONG in EEPROM every time it changes to avoid loss in case of a power failure. Reads it back on power up.
