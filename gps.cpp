#include "gps.h"
#define GPSDEBUG 1

HardwareSerial GPSSerial(1);

void gps::init()
{
  GPSSerial.begin(9600, SERIAL_8N1, GPS_TX, GPS_RX);
  GPSSerial.setTimeout(2);
}

void gps::encode()
{       
    int data;
    int previousMillis = millis();

    while((previousMillis + 1000) > millis())
    {
        while (GPSSerial.available() )
        {
            char data = GPSSerial.read();
            tGps.encode(data);
            #ifdef GPSDEBUG
              Serial.print(data);
            #endif
        }
    }
    #ifdef GPSDEBUG
     Serial.println("");
    #endif
}

void gps::buildPacket(uint8_t txBuffer[9])
{
  LatitudeBinary = ((tGps.location.lat() + 90) / 180.0) * 16777215;
  LongitudeBinary = ((tGps.location.lng() + 180) / 360.0) * 16777215;

  #ifdef GPSDEBUG
    sprintf(t, "Lat: %f", tGps.location.lat());
    Serial.println(t);
    
    sprintf(t, "Lng: %f", tGps.location.lng());
    Serial.println(t);
  #endif
  
  txBuffer[0] = ( LatitudeBinary >> 16 ) & 0xFF;
  txBuffer[1] = ( LatitudeBinary >> 8 ) & 0xFF;
  txBuffer[2] = LatitudeBinary & 0xFF;

  txBuffer[3] = ( LongitudeBinary >> 16 ) & 0xFF;
  txBuffer[4] = ( LongitudeBinary >> 8 ) & 0xFF;
  txBuffer[5] = LongitudeBinary & 0xFF;

  altitudeGps = tGps.altitude.meters();
  txBuffer[6] = ( altitudeGps >> 8 ) & 0xFF;
  txBuffer[7] = altitudeGps & 0xFF;

  hdopGps = tGps.hdop.value()/10;
  txBuffer[8] = hdopGps & 0xFF;
}

void gps::softwareReset() {
  GPSSerial.write(CFG_RST, sizeof(CFG_RST));
}

void gps::wakeup(){
  Serial.println("Wake");
  int data = -1;
  do{
    for(int i = 0; i < 20; i++){ //send random to trigger respose
        GPSSerial.write(0xFF);
      }
    data = GPSSerial.read();
  }while(data == -1);
  Serial.println("not sleeping");

}

void gps::enableSleep()
{
  do{ //We cannot read UBX ack therefore try to sleep gps until it does not send data anymore
    #ifdef GPSDEBUG
      Serial.println("try to sleep gps!");
    #endif
    gps::softwareReset(); //sleep_mode can only be activated at start up
    delay(600); //give some time to restart //TODO wait for ack
    GPSSerial.write(RXM_PMREQ, sizeof(RXM_PMREQ));
    unsigned long startTime = millis();
    unsigned long offTime = 1;
    Serial.println(offTime);
    
    while(millis() - startTime < 1000){ //wait for the last command to finish
      int c = GPSSerial.read();
      if(offTime == 1 && c == -1){ //check  if empty
        offTime = millis();
      }else if(c != -1){
        offTime = 1;
      }
      if(offTime != 1 && millis() - offTime > 100){ //if gps chip does not send any commands for .. seconds it is sleeping
        Serial.println("sleeping gps!");
        return;
      }
    }
  }while(1);
}

void gps::setLowPower()
{
    #ifdef GPSDEBUG
      Serial.println("try to set gps to low power mode");
    #endif
    gps::softwareReset(); //sleep_mode can only be activated at start up
    delay(600); //give some time to restart //TODO wait for ack
    GPSSerial.write(GPSpowersave, sizeof(GPSpowersave));
    delay(1000);
    
}

bool gps::checkGpsFix()
{
  encode();
  if (tGps.location.isValid() && 
      tGps.location.age() < 2000 &&
      tGps.hdop.isValid() &&
      tGps.hdop.value() <= 300 &&
      tGps.hdop.age() < 2000 &&
      tGps.altitude.isValid() && 
      tGps.altitude.age() < 2000 )
  {
    Serial.println("Valid gps Fix.");
    return true;
  }
  else
  {
     Serial.println("No gps Fix.");
    // sprintf(t, "location valid: %i" , tGps.location.isValid());
    // Serial.println(t);
    // sprintf(t, "location age: %i" , tGps.location.age());
    // Serial.println(t);
    // sprintf(t, "hdop valid: %i" , tGps.hdop.isValid());
    // Serial.println(t);
    // sprintf(t, "hdop age: %i" , tGps.hdop.age());
    // Serial.println(t);
    // sprintf(t, "hdop: %i" , tGps.hdop.value());
    // Serial.println(t);
    // sprintf(t, "altitude valid: %i" , tGps.altitude.isValid());
    // Serial.println(t);
    // sprintf(t, "altitude age: %i" , tGps.altitude.age());
    // Serial.println(t);

    return false;
  }
}

double gps::lat() 
{
  return tGps.location.lat();  
}

double gps::lng() 
{
  return tGps.location.lng();  
}
