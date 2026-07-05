#include <Arduino.h>
#include <DHT.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>

extern "C" {
  #include "user_interface.h"
  #include "gpio.h"
}

#include "I2CManager.h"
#include "DatabaseManager.h"

#define LOW_VOLTAGE_LED 16
#define DHT22_DATA 2
#define ADC_PIN A0
#define PUMP_PIN 14
#define RTC_int 12   
#define LOW_BAT_FLAG 13
#define SENSORS_ALIM_PIN 15
#define BTT_PIN 0   


#define SDA 4
#define SCL 5

#define MAX_READING_CYCLE  5
#define SAMPLING_READING_PERIOD_SEC 3

#define PUMP_STEP_MS 3

#define SSID "Plant-Watering-System"
#define PASS "Anturio00"
//sito web: 192.168.4.1



//================= VARS =================

bool rtc_alarm_flag = false;
bool isPumpReachPWMmax = false;
bool btt_pressed = false;




DHT dht = DHT(DHT22_DATA, DHT22);
I2C_Manager i2c = I2C_Manager(SDA, SCL); 
DatabaseManager database = DatabaseManager(SSID, PASS, i2c.RTC);
 

enum PUMP{
  ON = HIGH,
  OFF = LOW
};




//================= INTERRUPT ================

void IRAM_ATTR rtc_interrut()
{
  rtc_alarm_flag = true;
}

void IRAM_ATTR button_interrupt()
{
  btt_pressed = true;
}



//================= Utilities ================


void pumpController(PUMP status)
{
  if (status == ON && !isPumpReachPWMmax)
  {
    for (int i=0; i<255; i++)
    {
      analogWrite(PUMP_PIN, i);
      delay(PUMP_STEP_MS);
    }

    int pwm_value = ( (float) database.PARMS.getPWM_rate()/100) * 255;
    int ms_delay = database.PARMS.getTimePumpFullPWM_sec() * 1000; 
    delay(ms_delay);
    analogWrite(PUMP_PIN, pwm_value);

    isPumpReachPWMmax = true;
  }
  else if (status == OFF)
  {
    digitalWrite(PUMP_PIN, OFF);
    isPumpReachPWMmax = false;
  }
}



Reading readingSensors()
{
  float airHum = dht.readHumidity();
  float tempC = dht.readTemperature();
  
  int adc_value = analogRead(ADC_PIN);
  float soilHum = constrain(100.0f - (adc_value / 1023.0f) * 100.0f, 0.0f, 100.0f);

  Reading read;
  read.AirHum = airHum;
  read.AirTemp = tempC;
  read.SoilHum = soilHum;
  read.TimeStamp = i2c.RTC.getTimeStamp();

  return read;
}




void setup() {
  pinMode(SENSORS_ALIM_PIN, OUTPUT);
  pinMode(LOW_VOLTAGE_LED, OUTPUT);
  pinMode(DHT22_DATA, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(LOW_BAT_FLAG, INPUT); 
  pinMode(BTT_PIN, INPUT);
  pinMode(ADC_PIN, INPUT);
  pinMode(RTC_int, INPUT);  
  
  attachInterrupt(digitalPinToInterrupt(RTC_int), rtc_interrut, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTT_PIN), button_interrupt, FALLING);

  dht.begin();
  Serial.begin(115200);
  Serial.println("BOOT");

  i2c.begin();
  if (!i2c.OLED.begin()) 
  {
    Serial.println(F("OLED init failed"));
  }

  if (!i2c.RTC.begin())
  {
    Serial.println(F("RTC init failed"));
  }

  database.WIFI.startAccessPoint();
  i2c.OLED.setDisplayOn();
  i2c.OLED.Print("Testing mode");
  digitalWrite(SENSORS_ALIM_PIN, ON);

  i2c.RTC.resetTimer();
  i2c.RTC.startTimer_sec(1);
}


void loop() 
{ 
  if (rtc_alarm_flag == true)
    {
      Reading read = readingSensors();
      read.isPumpToActivate = database.PARMS.getPumpStatus();
      database.insert(read);
      i2c.OLED.clearScreen();
      i2c.OLED.displaySensorValues(read.SoilHum, read.AirHum, read.AirTemp);
      

      i2c.RTC.resetTimer();
      i2c.RTC.startTimer_sec(3);
      rtc_alarm_flag = false;
    }

  PUMP pump = database.PARMS.getPumpStatus() ? ON : OFF;
  pumpController(pump); 


  if (btt_pressed)
  {
    btt_pressed = false; 
    database._clear_file();
    i2c.OLED.clearScreen();
    i2c.OLED.Print("Database clear");
  }
}
