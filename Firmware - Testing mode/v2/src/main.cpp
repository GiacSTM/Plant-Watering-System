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

const int readDHTms = 1000;
const int readMOISTms = 100;
const int reading_secs = 5;

bool inReading = false;
bool isPumpReachPWMmax = false;
bool btt_pressed = false;

const int N_READS = (reading_secs * 1000 / readDHTms) + (reading_secs * 1000 / readMOISTms);
Reading reads[N_READS];
int read_counter = 0;


DHT dht = DHT(DHT22_DATA, DHT22);
I2C_Manager i2c = I2C_Manager(SDA, SCL); 
DatabaseManager database = DatabaseManager(SSID, PASS, i2c.RTC);
 

enum PUMP{
  ON = HIGH,
  OFF = LOW
};




//================= INTERRUPT ================

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



Reading readingSensors(bool dht22, bool soil)
{
  float airHum = 0.0;
  float tempC = 0.0;
  float soilHum = 0.0;
  Reading read;

  if (dht22)
  {
    airHum = dht.readHumidity();
    tempC = dht.readTemperature();
    read.AirHum = airHum;
    read.AirTemp = tempC;
    read.SoilHum = -1.0;
  }
  
  if (soil)
  {
    int adc_value = analogRead(ADC_PIN);
    float soilHum = constrain(100.0f - (adc_value / 1023.0f) * 100.0f, 0.0f, 100.0f);
    read.SoilHum = soilHum;
    read.AirHum = -1.0;
    read.AirTemp = -1.0;
  }
  
  read.TimeStamp = 0; // lo calcolo solo alla fine
  read.isPumpToActivate = 0;

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
  i2c.OLED.Print("TESTING MODE V2");
  digitalWrite(SENSORS_ALIM_PIN, ON);
}

unsigned long millis_dht = readDHTms;
unsigned long millis_soil = readMOISTms;

void loop() 
{ 
  // get final result and start new reading after 2 secs
  if (read_counter >= N_READS)
  {
    float soilHumSum = 0.0;
    float AirTempSum = 0.0;
    float AirHumSum = 0.0;
    int dht_reads = 0;
    int soil_reads = 0;


    for (int i=0; i<read_counter; i++)
    {
      Reading read = reads[i];

      if (read.SoilHum == -1)
      {
        AirTempSum += read.AirTemp;
        AirHumSum += read.AirHum;
        dht_reads ++;
      }
      else
      {
        soilHumSum += read.SoilHum;
        soil_reads ++;
      }
    }

    Reading finalRead;
    finalRead.AirHum = AirHumSum / dht_reads;
    finalRead.AirTemp = AirTempSum / dht_reads;
    finalRead.SoilHum = soilHumSum / soil_reads;

    i2c.OLED.displaySensorValues(finalRead.SoilHum, finalRead.AirHum, finalRead.AirTemp );
    finalRead.isPumpToActivate = database.PARMS.getPumpStatus();
    finalRead.TimeStamp = i2c.RTC.getTimeStamp();
    database.insert(finalRead);

    read_counter = 0; 
  }
  

  if ((millis() - millis_dht) >= readDHTms)
  {
    reads[read_counter++] = readingSensors(true, false);
    millis_dht = millis();
  }
  if ((millis() - millis_soil) >= readMOISTms)
  {
    reads[read_counter++] = readingSensors(false, true);
    millis_soil = millis();
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
