## Intro

This is a simple sketch demonstrating the capability of the [TTGO T-Beam](https://www.aliexpress.com/store/product/TTGO-T-Beam-ESP32-433-868-915Mhz-WiFi-wireless-Bluetooth-Module-ESP-32-GPS-NEO-6M/2090076_32875743018.html) as a [TTN Mapper](https://ttnmapper.org/) Node on [The Things Network](https://www.thethingsnetwork.org/) LoraWAN.

Derived from [sbiermann/Lora-TTNMapper-ESP32](https://github.com/sbiermann/Lora-TTNMapper-ESP32) and with some information/inspiration from [cyberman54/ESP32-Paxcounter](https://github.com/cyberman54/ESP32-Paxcounter) and [Edzelf/LoRa](https://github.com/Edzelf/LoRa).

Modified and enhanced by dermatthias with sleep and OTAA features for this fork. Additional sleepytimes for the GPS module enabling lower-power-ish operations by stk, based on work by [JoepSchyns/Low_power_TTGO_T-Beam](https://github.com/JoepSchyns/Low_power_TTGO_T-beam) and helpful [ublox documentation in the UKHAS Wiki](https://ukhas.org.uk/guides:ublox_psm). This also measures and transmits battery voltage.

## Software dependencies

Arduino IDE [ESP32 extension](https://github.com/espressif/arduino-esp32)

[TinyGPS++](http://arduiniana.org/libraries/tinygpsplus/)

[MCCI LMIC Arduino](https://github.com/mcci-catena/arduino-lmic) This is a hell to configure. You need to edit `~/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/project_config/lmic_project_config.h` and define the “right” band, e.g. `CFG_eu868` (and comment out non-matching others)

## Instructions

_If you have an older hardware version:_ You need to connect the [T-Beam](https://github.com/LilyGO/TTGO-T-Beam) DIO1 pin marked *Lora1* to the *pin 33* - So that the ESP32 can read that output from the Lora module.
Optionally you can also connect the *Lora2* output to *GPIO 32*, but this is not needed here.

You can program the T-Beam using the [Arduino ESP32](https://github.com/espressif/arduino-esp32) board 'Heltec_WIFI_LoRa_32'.

On The Things Network side, the settings needed are available [here](https://www.thethingsnetwork.org/docs/applications/ttnmapper/).

Configure the Payload decoder with:
```javascript
function Decoder(bytes, port) {
    var decoded = {};

    decoded.latitude = ((bytes[0]<<16)>>>0) + ((bytes[1]<<8)>>>0) + bytes[2];
    decoded.latitude = (decoded.latitude / 16777215.0 * 180) - 90;
  
    decoded.longitude = ((bytes[3]<<16)>>>0) + ((bytes[4]<<8)>>>0) + bytes[5];
    decoded.longitude = (decoded.longitude / 16777215.0 * 360) - 180;
  
    var altValue = ((bytes[6]<<8)>>>0) + bytes[7];
    var sign = bytes[6] & (1 << 7);
    if(sign)
    {
        decoded.altitude = 0xFFFF0000 | altValue;
    }
    else
    {
        decoded.altitude = altValue;
    }
  
    decoded.hdop = bytes[8] / 10.0;

    decoded.vbat = ((bytes[9]<<8) + bytes[10]) / 100;

    return decoded;
}
```

Let me know if more detailed instructions are needed.

## Todolist

* ~~Stop sending data to TTN until the GPS get a fix.~~ <== Done thanks to [@Roeland54](https://github.com/Roeland54)
* Manage and document the different T-Beam revisions/versions.
* ~~Switch to OTAA auth method for TTN and save the 'credentials' for reboot use~~.
* ~~Save and reload the frame counter somewhere - GPS RTC data ? SPIFFS ? EEPROM ? - so I can check the "Frame Counter Checks" box as recommended on TTN~~.
* Also save the GPS 'status' so that on next boot it gets a fix faster.
* ~~Reduce the power needed ! That thing is a power hog currently, we need to make it sleep most of the time as possible~~.
* Adapt the data send frequency based on current velocity : When not moving, an update per hour should be enough. ← this will be a future step including cheap accelerometers.

Let me know if you think anything else would make sense for a TTN mapper node : Open an issue, I will consider it.

