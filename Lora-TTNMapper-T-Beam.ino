#include <lmic.h>
#include <hal/hal.h>
#include <WiFi.h>

// UPDATE the config.h file in the same folder WITH YOUR TTN KEYS AND ADDR.
#include "config.h"
#include "gps.h"

// #define DEBUG 1

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

#ifdef DEBUG
  #define TIME_TO_SLEEP  30        /* Time ESP32 will go to sleep (in seconds) */
  #define STATCOUNT 9
#else
  #define TIME_TO_SLEEP  90
  #define STATCOUNT 9
#endif

// T-Beam specific hardware
#define BUILTIN_LED 14
#define BATTERY_VOLTAGE 35

// OTAA (true) or ABP (false)
#define USE_OTAA true

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR u4_t RTC_seqnoUp = 0;
RTC_DATA_ATTR int otaaJoined = 0;
RTC_DATA_ATTR u1_t keep_nwkKey[16] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
RTC_DATA_ATTR u1_t keep_artKey[16] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
RTC_DATA_ATTR devaddr_t keep_devaddr = 0;

RTC_DATA_ATTR double prevLat = 0;
RTC_DATA_ATTR double prevLon = 0;
RTC_DATA_ATTR int statCount = 0;
double dist = 0;

char s[32]; // used to sprintf for Serial output
uint8_t txBuffer[11];
gps gps;

// OTAA, see config.h for settings
#if defined(USE_OTAA) && USE_OTAA == true
void os_getArtEui(u1_t * buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t * buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t * buf) { memcpy_P(buf, APPKEY, 16); }
#else
// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui(u1_t * buf) {}
void os_getDevEui(u1_t * buf) {}
void os_getDevKey(u1_t * buf) {}
#endif

static osjob_t sendjob;
// Schedule TX every this many seconds (might become longer due to duty cycle limitations).
const unsigned TX_INTERVAL = 30;

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = LMIC_UNUSED_PIN, // was "14,"
  .dio = {26, 33, 32},
};

void onEvent (ev_t ev) {
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      // Disable link check validation (automatically enabled
      // during join, but not supported by TTN at this time).
      Serial.println(F("Saving OTAA values after successfull join."));
      memcpy(keep_nwkKey, LMIC.nwkKey, 16);
      memcpy(keep_artKey, LMIC.artKey, 16);
      keep_devaddr = LMIC.devaddr;

      for(int i = 0; i < 16; i++) {
        Serial.print("0x");
        Serial.print(LMIC.nwkKey[i], HEX);
        Serial.print(" ");
      }
      Serial.println("");

      otaaJoined = 1;
      LMIC_setLinkCheckMode(0);
      break;
    case EV_RFU1:
      Serial.println(F("EV_RFU1"));
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            digitalWrite(BUILTIN_LED, LOW);
      if (LMIC.txrxFlags & TXRX_ACK) {
        Serial.println(F("Received Ack"));
      }
      if (LMIC.dataLen) {
        sprintf(s, "Received %i bytes of payload", LMIC.dataLen);
        Serial.println(s);
        sprintf(s, "RSSI %d SNR %.1d", LMIC.rssi, LMIC.snr);
        Serial.println(s);
      }
      // Schedule next transmission
      // os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      // go into deep sleep for TX_interval
      RTC_seqnoUp = LMIC.seqnoUp;
      gps.enableSleep();
      // gps.setLowPower();
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      esp_deep_sleep_start();
      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    default:
      Serial.println(F("Unknown event"));
      break;
  }
}

float getBatteryVoltage() {
  float vBat = analogRead(BATTERY_VOLTAGE) *2 *3.3 /1024;
  return vBat;
}

void do_send(osjob_t *j) {

  uint16_t currentVoltage = getBatteryVoltage() * 100;
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND)
  {
    Serial.println(F("OP_TXRXPEND, not sending"));
  }
  else
  { 
    if (gps.checkGpsFix())
    {
      dist = TinyGPSPlus::distanceBetween(gps.lat(), gps.lng(), prevLat, prevLon);
      if (dist > 50 || statCount > STATCOUNT) {
        Serial.println("Distance moved: " + String(dist));
        Serial.println("Time stationary: " + String(statCount * TIME_TO_SLEEP * uS_TO_S_FACTOR));
        if (dist <= 50) {
          Serial.println("Sending because stationary for longer than max.");
        }
        statCount = 0;
        prevLat = gps.lat();
        prevLon = gps.lng();
        // Prepare upstream data transmission at the next possible time.
        gps.buildPacket(txBuffer);
        txBuffer[9] = (currentVoltage >> 8);
        txBuffer[10] = currentVoltage;
        LMIC_setTxData2(1, txBuffer, sizeof(txBuffer), 0);
        Serial.println(F("Packet queued"));
        digitalWrite(BUILTIN_LED, HIGH);
      } else {
        Serial.println("Not sending, stationary.");
        Serial.println("Distance moved: " + String(dist));
        Serial.println("Time stationary: " + String(statCount * TIME_TO_SLEEP * uS_TO_S_FACTOR));
        ++statCount;
        RTC_seqnoUp = LMIC.seqnoUp;
        gps.enableSleep();
        // gps.setLowPower();
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
        esp_deep_sleep_start();        
      }
    }
    else
    {
      //try again in 3 seconds
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(3), do_send);
    }
  }
  // Next TX is scheduled after TX_COMPLETE event.
}


void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER  : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD  : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  //Increment boot number and print it every reboot
  ++bootCount;
  #ifdef DEBUG
    Serial.println("Boot number: " + String(bootCount));
    Serial.println("RTC_seqnoUp: " + String(RTC_seqnoUp));
    Serial.println("Stationary Counter: " + String(statCount));       
  
    //Print the wakeup reason for ESP32
    print_wakeup_reason();    
  #endif
  
    //*************************
    // ESP32 and LMIC
    //*************************
  //Turn off WiFi and Bluetooth
  WiFi.mode(WIFI_OFF);
  btStop();
  gps.init();
  gps.softwareReset();

  // Setup ADC to measure battery voltage
  adcAttachPin(BATTERY_VOLTAGE);
  adcStart(BATTERY_VOLTAGE);
  analogReadResolution(10);

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // ABP or OTAA
  #if defined(USE_OTAA) && USE_OTAA == true
    #ifdef DEBUG
      Serial.println("Activation method: OTAA");
    #endif
    if (bootCount > 0 && otaaJoined == 1) {
        Serial.println(F("Restoring OTAA session credentials from memory"));
        memcpy(LMIC.nwkKey, keep_nwkKey, 16);
        memcpy(LMIC.artKey, keep_artKey, 16);
        LMIC.devaddr = keep_devaddr;
      }
  #else
    #ifdef DEBUG
      Serial.println("Activation method: ABP");
    #endif
    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
  #endif
  
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band

  // Disable link check validation
  LMIC_setLinkCheckMode(0);

  // TTN uses SF9 for its RX2 window.
  LMIC.dn2Dr = DR_SF9;

  // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
  LMIC_setDrTxpow(DR_SF7,14); 

  LMIC.seqnoUp = RTC_seqnoUp;
  LMIC.seqnoDn = RTC_seqnoUp;

  do_send(&sendjob);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);
  
}

void loop() {
    os_runloop_once();
}
