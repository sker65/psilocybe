Psilocybe Lamp
=========

Control an addressable LED strip with an ESP8266 via a web browser for a Psilocbye lamp see https://www.evilgeniuslabs.org/psilocybe

Hardware
--------

Building the lamp see https://www.evilgeniuslabs.org/psilocybe it uses 241 addressable RGB leds as rings e.g. https://de.aliexpress.com/item/1005004109462341.html and a ESP device like d1 mini or similar. (https://de.aliexpress.com/item/1005003631086102.html)

To control the data line of the led rings it is recommended to use a level shifter or a driver e.g. HCT 245.

<img src="./images/lamp.jpg" width="200" align="right">To make it nice looking the led rings should be behind a diffusor. Easiest and nice way to do this, is to reuse a regular lamp like this 
https://toom.de/p/deckenleuchte-weisssatiniert-mars/9832229 

This how it looks (Jasons video): https://youtu.be/mms5l1I2q0M

<img src="./images/psilo.jpg"> image courtesy of jason

Firmware
--------
Firmware is a fork of Jasons initial version https://github.com/evilgeniuslabs/psilocybe but extended with some convenient addons:

- mqtt control and status for intergration in e.g. home assistant
- WifiManager for easier setup of wifi network
- OTA for easier firmware updates
- speed control for almost all effects

WEB UI
------
The embedded webserver presents a nice UI to control the lamp. The UI is updated in realtime even if you control the device via mqtt.
<img src="./images/ui.jpg">

MQTT 
------------
This is an example how the Psilocybe can be configured as a home assistant mqtt light device:
````
- name: 'Psilocybe'
  state_topic: psilocybe/psilo1/state
  state_value_template: '{{ json_value.power }}'
  payload_on: 'on'
  payload_off: 'off'
  command_topic: psilocybe/psilo1/cmd/power
  brightness_state_topic: psilocybe/psilo1/state
  brightness_value_template: '{{json_value["brightness"]}}'
  brightness_command_topic: psilocybe/psilo1/cmd/brightness
  rgb_command_topic: psilocybe/psilo1/cmd/solidcolor
  rgb_state_topic: psilocybe/psilo1/state
  rgb_value_template: "{{ json_value.solidcolor[1:3] | int(base=16) ~ ',' ~ json_value.solidcolor[3:5] | int(base=16) ~ ',' ~ json_value.solidcolor[5:7] | int(base=16) }}"
  effect_list: ['Pride', 'Color Waves']
  effect_command_topic: psilocybe/psilo1/cmd/pattern
  effect_state_topic: psilocybe/psilo1/state
  effect_value_template: '{{json_value["pattern"]}}'

````
MQTT support in not completed yet, but all basic functions work already.

MQTT is configureable you can set server name and prefix, and enable or diable support.

Accepted commands are:
    <ul>
      <li>power: 0|1 to switch on and off</li>
      <li>brightness: 0..255 to set brightness</li>
      <li>autoplay: 0|1 to start/stop autoplay</li>
      <li>palette: palette index</li>
      <li>pattern: effect pattern index</li>
      <li>autoplayDuration: duration of autoplay for one effect</li>
      <li>solidcolor: hexstring for color. format #RRGGBB</li>
    </ul>

As soon as the device is online it publishs an info message on the topic
&lt;topic&gt;/info with version infos for the firmware, e.g.:
````
{ "clientId": "ESP8266Client-xxx", "IP": "x.x.x.x","version":"0.1" }
````
## MQTT config

<img src="./images/config-mqtt.jpg">

# Other improvements
This firmware has some convenience features that were not part of the original firmware:

- **OTA firmware update**: uses ArduinoOTA library, so that firmware and web ui data can be updated directly from ArduinoIDE over the air
- **WiFiManager**: if no wifi is configured or wifi not in reach, a self service access point is started, with UI to pick wifi network and enter password
