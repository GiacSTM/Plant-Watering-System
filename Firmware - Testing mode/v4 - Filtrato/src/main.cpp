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
const int reading_secs = 10;

bool inReading = false;
bool isPumpReachPWMmax = false;
bool btt_pressed = false;

const int N_READS = (reading_secs * 1000 / readDHTms) + (reading_secs * 1000 / readMOISTms);
int read_counter = 0;
Reading reads[N_READS];

// Buffer per il ButterWorth
float X_SOIL[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
float Y_SOIL[5] = {0.0, 0.0, 0.0, 0.0, 0.0};

// Coefficienti ButterWorth 
double a[5] = {1.0, -3.67172901, 5.06799839, -3.115966925, 0.719910327};
double b[5] = {1.32937289e-05, 5.31749156e-05, 7.976237339e-05, 5.31749156e-05, 1.32937289e-05};



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

/*
 * Equazione alle differenze del filtro Butterworth del 4° ordine:
 * * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + b3*x[n-3] + b4*x[n-4]
 * - a1*y[n-1] - a2*y[n-2] - a3*y[n-3] - a4*y[n-4]
 */
float butterworth()
{
  float y = 0;

  for (int i=0; i<5; i++)
  {
    y += b[i]*X_SOIL[i];
  }
  for (int i=1; i<5; i++)
  {
    y -= a[i]*Y_SOIL[i];
  }

  return y;
}

void shiftARRAY(float array[])
{
  for (int i = 4; i > 0; i--)
  {
    array[i] = array[i-1];
  }
}


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
  digitalWrite(SENSORS_ALIM_PIN, ON);
  i2c.OLED.Print("TESTING MODE V4");
  

  //transitorio
  readingSensors(false, true); delay(10);
  readingSensors(false, true); delay(10);

  Reading read = readingSensors(false, true);
  for (int i=0; i<5; i++)
  {
    X_SOIL[i] = read.SoilHum;
    Y_SOIL[i] = read.SoilHum;
  }
  
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
    Reading read = readingSensors(false, true);
    X_SOIL[0] = read.SoilHum;
    Y_SOIL[0] = butterworth();    // la misura filtrata
    read.SoilHum = Y_SOIL[0];
    
    shiftARRAY(X_SOIL);
    shiftARRAY(Y_SOIL);

    reads[read_counter++] = read;

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
