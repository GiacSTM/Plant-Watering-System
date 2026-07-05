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

#define ALPHA 0.25
#define PUMP_STEP_MS 3

#define SSID "Plant-Watering-System"
#define PASS "Anturio00"
//sito web: 192.168.4.1



//================= VARS =================

bool btt_change_mode = false;
bool btt_changed_level = false;
bool last_stable_level = HIGH;    // pull-up
unsigned long start_press_time = 0;

bool rtc_alarm_flag = false;        
bool is_pump_full = false;                                                                    
bool isPumping = false;
float partial_target_hum;

const int readDHTms = 1000;
const int readMOISTms = 100;
const int reading_secs = 10;

const int N_READS = (reading_secs * 1000 / readDHTms) + (reading_secs * 1000 / readMOISTms);
int read_counter = 0;
Reading reads[N_READS];

DHT dht = DHT(DHT22_DATA, DHT22);
Ticker timer = Ticker();
I2C_Manager i2c = I2C_Manager(SDA, SCL); 
DatabaseManager database = DatabaseManager(SSID, PASS, i2c.RTC);

// Buffer per il ButterWorth
float X_SOIL[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
float Y_SOIL[5] = {0.0, 0.0, 0.0, 0.0, 0.0};

// Coefficienti ButterWorth 
double a[5] = {1.0, -3.67172901, 5.06799839, -3.115966925, 0.719910327};
double b[5] = {1.32937289e-05, 5.31749156e-05, 7.976237339e-05, 5.31749156e-05, 1.32937289e-05};

 

enum PUMP{
  ON = HIGH,
  OFF = LOW
};

enum SYSTEM_STATES{
  /* 
    NORMAL: il sistema sta facendo una misura ogni SAMPLING_SEC
    BROADCAST: il sistema misura in modo continuo, WIFI e Display sono accesi
    ALARM: Soglia batteria < 3v0 volt, tutto blocato con led acceso
  */

  NORMAL = 0,
  BROADCAST = 1,
  ALARM = 2,
  READING_ROUTINE = 3,
  PUMP_ROUTINE = 4
};

struct times
{
  unsigned long millis_dht;
  unsigned long millis_soil;
};


SYSTEM_STATES stato = NORMAL;
SYSTEM_STATES previous_state;
times TIMES;





//================= INTERRUPT ================

void IRAM_ATTR btt_interrupt()  
{
  if (stato != ALARM)
  {
    btt_changed_level = true;
  }
    
}


void IRAM_ATTR batt_low_int()
{
  /*
    LOW: ALLARM
    HIGH: NO ALARM
  */

  if (digitalRead(LOW_BAT_FLAG) == LOW)
    stato = ALARM;
  else
    stato = NORMAL;
}

void IRAM_ATTR rtc_interrut()
{
  if (stato != ALARM)
    rtc_alarm_flag = true;
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
  if (status == ON && !is_pump_full)
  {
    for (int i=0; i<255; i++)
    {
      analogWrite(PUMP_PIN, i);
      delay(PUMP_STEP_MS);
    }
    is_pump_full = true;
  }
  else if (status == OFF)
  {
    digitalWrite(PUMP_PIN, OFF);
    is_pump_full = false;
  }
}


void firstChecks()
{
  if (!digitalRead(LOW_BAT_FLAG))
    stato = ALARM;

  // misura all'avvio
  stato = READING_ROUTINE;
  previous_state = NORMAL;
}


Reading readingSensors(bool dht22, bool soil)
{
  float airHum = 0.0;
  float tempC = 0.0;
  float soilHum = 0.0;
  Reading read;

  if (dht22 && !soil)
  {
    airHum = dht.readHumidity();
    tempC = dht.readTemperature();
    read.AirHum = airHum;
    read.AirTemp = tempC;
    read.SoilHum = -1.0;
  }
  else if (soil && !dht22)
  {
    int adc_value = analogRead(ADC_PIN);
    soilHum = constrain(100.0f - (adc_value / 1023.0f) * 100.0f, 0.0f, 100.0f);
    read.SoilHum = soilHum;
    read.AirHum = -1.0;
    read.AirTemp = -1.0;
  }
  else
  {
    airHum = dht.readHumidity();
    tempC = dht.readTemperature();
    int adc_value = analogRead(ADC_PIN);
    soilHum = constrain(100.0f - (adc_value / 1023.0f) * 100.0f, 0.0f, 100.0f);

    read.AirHum = airHum;
    read.AirTemp = tempC;
    read.SoilHum = soilHum;
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
  pinMode(RTC_int, INPUT_PULLUP);  
  
  attachInterrupt(digitalPinToInterrupt(BTT_PIN), btt_interrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(LOW_BAT_FLAG), batt_low_int, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RTC_int), rtc_interrut, FALLING);

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


  //transitorio
  digitalWrite(SENSORS_ALIM_PIN, ON);
  delay(2000);

  // scarto un po di campioni
  for (int i=0; i<20; i++)
  {
    readingSensors(false, true); 
    delay(100);
  }
    
  // carico buffer del filtro
  Reading read = readingSensors(false, true);
  for (int i=0; i<5; i++)
  {
    X_SOIL[i] = read.SoilHum;
    Y_SOIL[i] = read.SoilHum;
  }
  
  firstChecks();  

  i2c.OLED.setDisplayOn();
  i2c.OLED.Print("STARTING...");

  // iniziazizzazione tempi di misura
  TIMES.millis_dht = readDHTms;
  TIMES.millis_soil = readMOISTms;
}


void loop() 
{ 
  if (stato == NORMAL)
  {
    digitalWrite(SENSORS_ALIM_PIN, OFF);
    digitalWrite(LOW_VOLTAGE_LED, OFF);
    i2c.OLED.setDisplayOff();

    // per il controllo manuale
    PUMP pump = database.PARMS.getPumpStatus() ? ON : OFF;
    pumpController(pump); 

  }
  else if (stato == BROADCAST)
  {
    digitalWrite(LOW_VOLTAGE_LED, OFF);
    
    i2c.OLED.setDisplayOn();
    digitalWrite(SENSORS_ALIM_PIN, ON);


    Reading read = readingSensors(true, true);
    X_SOIL[0] = read.SoilHum;
    Y_SOIL[0] = butterworth();    // la misura filtrata
    read.SoilHum = Y_SOIL[0];
    
    shiftARRAY(X_SOIL);
    shiftARRAY(Y_SOIL);

    if ( !isnan(read.AirHum) ||  !isnan(read.AirTemp) )
      i2c.OLED.displaySensorValues(read.SoilHum, read.AirHum, read.AirTemp);

    // per il controllo manuale
    PUMP pump = database.PARMS.getPumpStatus() ? ON : OFF;
    pumpController(pump); 

  }
  else if(stato == ALARM)
  {
    digitalWrite(LOW_VOLTAGE_LED, ON);

    i2c.OLED.clearScreen();
    i2c.OLED.setDisplayOff();
    pumpController(OFF);
    database.PARMS.setPumpStatus(false);
    database.WIFI.stopAccessPoint();
  }
  else if (stato == READING_ROUTINE)
  {

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

      if (finalRead.SoilHum < database.PARMS.getSoilHumTargetLOW())
      {
        stato = PUMP_ROUTINE;
        finalRead.isPumpToActivate = true;
        i2c.RTC.clearAlarm();
      }
      else
      {
        stato = previous_state;
        finalRead.isPumpToActivate = false;
        digitalWrite(SENSORS_ALIM_PIN, OFF);

        i2c.RTC.clearAlarm();
        delay(10);
        i2c.RTC.startAlarm();
      }

      finalRead.TimeStamp = i2c.RTC.getTimeStamp();
      database.insert(finalRead);
      i2c.OLED.clearScreen();

      read_counter = 0; 
    }
    else
    {
      if ((millis() - TIMES.millis_dht) >= readDHTms && (read_counter < N_READS))
      {
        reads[read_counter++] = readingSensors(true, false);
        TIMES.millis_dht = millis();
      }
      if ((millis() - TIMES.millis_soil) >= readMOISTms && (read_counter < N_READS))
      {
        
        Reading read = readingSensors(false, true);
        X_SOIL[0] = read.SoilHum;
        Y_SOIL[0] = butterworth();    // la misura filtrata
        read.SoilHum = Y_SOIL[0];
        
        shiftARRAY(X_SOIL);
        shiftARRAY(Y_SOIL);

        reads[read_counter++] = read;

        TIMES.millis_soil = millis();
      }
    }
  }
  else if (stato == PUMP_ROUTINE)
  {
    if (!isPumping)     
    {
      isPumping = true;

      Reading lastRead;
      database.getRecordsPage(&lastRead, 1, 0);
       
      i2c.OLED.clearScreen();
      i2c.OLED.Print("PUMPING...");
      
      // ricalcolo parameri
      int low = database.PARMS.getSoilHumTargetLOW();
      int high = database.PARMS.getSoilHumTargetHIGH();

      if (lastRead.AirTemp > 30) low += 5;
      if (lastRead.AirTemp < 16) low -= 5;
      if (lastRead.AirHum > 70) high -= 5;
      if (lastRead.AirHum < 35) high += 5;
      
      // Controllo di sicurezza
      if ((high - low) < 10)
      {
        int deficit = 10 - (high - low);
        high += deficit / 2;
        low  -= (deficit - (deficit / 2)); 
      }

      partial_target_hum = (float)high - (ALPHA * (float)(high - low));
      pumpController(ON);
      database.PARMS.setPumpStatus(true);
    }

    // misura
    delay(readMOISTms);
    Reading read = readingSensors(false, true);
    X_SOIL[0] = read.SoilHum;
    Y_SOIL[0] = butterworth();
    float hum_filtrata = Y_SOIL[0];

    shiftARRAY(X_SOIL);
    shiftARRAY(Y_SOIL);

    if (hum_filtrata >= partial_target_hum)
    {
      isPumping = false;
      pumpController(OFF);
      i2c.OLED.clearScreen();
      database.PARMS.setPumpStatus(false);

      stato = previous_state;

      int oldAlarmParms[2];
      i2c.RTC.getAlarmParms(oldAlarmParms);

      // 10 minuti per vedere se l'acqua bastaq
      i2c.RTC.setAlarmParms(0, 10);
      i2c.RTC.clearAlarm();
      delay(10);
      i2c.RTC.startAlarm();

      i2c.RTC.setAlarmParms(oldAlarmParms[0], oldAlarmParms[1]);
    }
  }


  if (btt_changed_level)
  {

    // controllo antirimbalzo
    bool new_level = digitalRead(BTT_PIN);
    if (new_level != last_stable_level)
    {
      last_stable_level = new_level;

      // PREMUTO (pull-up)
      if (new_level == LOW)
      {
        start_press_time = millis();
      }
      else
      {
        // RILASCIATO
        unsigned long pressed_time = millis() - start_press_time;
        if (pressed_time >= 3000)
        {
          //reset
          i2c.OLED.setDisplayOn();
          i2c.OLED.clearScreen();
          i2c.OLED.Print("RESET CHIP and MEMORY");
          database._clear_file();

          delay(1000);
          ESP.restart();
        }
        else
        {
          btt_change_mode = true;
        }
      }
    }

    btt_changed_level = false;
  }


  if(btt_change_mode)
  {
    /*  
      1) Gia controllato nell'interrupt che non è in allarme
      2) Per evitare danni non si puo premere il pulsante durante il primo check
    */ 

    if (stato != READING_ROUTINE && stato != PUMP_ROUTINE)
    {
      if (stato != BROADCAST)
      {
        stato = BROADCAST;
        database.WIFI.startAccessPoint();
      }
      else 
      {
        stato = NORMAL;
        database.WIFI.stopAccessPoint();
      }
    }

    btt_change_mode = false;
  
  }

 if (rtc_alarm_flag)
  {
    rtc_alarm_flag = false;

    if (stato == NORMAL || stato == BROADCAST)
    {
      previous_state = stato;
      stato = READING_ROUTINE;

      digitalWrite(SENSORS_ALIM_PIN, ON);
      delay(500);
        
      i2c.OLED.clearScreen();
      i2c.OLED.Print("EVALUATION");

      i2c.RTC.clearAlarm(); 
    }
  }
    
}
