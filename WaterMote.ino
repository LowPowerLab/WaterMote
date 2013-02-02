/*
  WaterMeter.cpp - Arduino/Moteino sketch for reading a SY310 based water/pulse meter
  Copyright (c) 2013 Felix Rusu (felix@lowpowerlab.com).  All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <EEPROM.h>
#include <TimerOne.h>
#include <RFM12B.h>

#define NODEID       22  //network ID used for this unit
#define NETWORKID    99  //the network ID we are on
#define GATEWAYID     1  //the node ID we're sending to
#define LED           9
#define INPUTPIN      1  //INT1 = digital pin 3 (must be an interrupt pin!)

#define SERIAL_EN        //uncomment this line to enable serial IO (when you debug Moteino and need serial output)
#define SERIAL_BAUD  115200
#ifdef SERIAL_EN
  #define DEBUG(input)   {Serial.print(input);}
  #define DEBUGln(input) {Serial.println(input);}
#else
  #define DEBUG(input);
  #define DEBUGln(input);
#endif

RFM12B radio;
uint8_t KEY[] = "ABCDABCDABCDABCD"; //Encryption key used by RFM12B, must be 16bytes, and you should change it!

int ledState = LOW;
volatile unsigned long PulseCounterVolatile = 0; // use volatile for shared variables
unsigned long PulseCounter = 0;
float GAL;
byte PulsesPerGAL = 45;
volatile unsigned long NOW = 0;
unsigned long LASTMINUTEMARK = 0;
unsigned long PULSECOUNTLASTMINUTEMARK = 0; //keeps pulse count at the last minute mark

byte COUNTEREEPROMSLOTS = 10;
unsigned long COUNTERADDRBASE = 8; //address in EEPROM that points to the first possible slot for a counter
unsigned long COUNTERADDR = 0;     //address in EEPROM that points to the latest Counter in EEPROM
byte secondCounter = 0;

unsigned long TIMESTAMP_pulse_prev = 0;
unsigned long TIMESTAMP_pulse_curr = 0;
int GPMthreshold = 8000;         // GPM will reset after this many MS if no pulses are registered
int XMIT_Interval = 5000;        // GPMthreshold should be less than 2*XMIT_Interval
int pulseAVGInterval = 0;
int pulsesPerXMITperiod = 0;
float GPM=0;
char sendBuf[RF12_MAXDATA];
byte sendLen;

void setup() {
  radio.Initialize(NODEID, RF12_433MHZ, NETWORKID);
  radio.Encrypt(KEY);
  radio.Sleep(); //sleep right away to save power
  pinMode(LED, OUTPUT);

  //initialize counter from EEPROM
  unsigned long savedCounter = EEPROM_Read_Counter();
  if (savedCounter <=0) savedCounter = 1; //avoid division by 0
  PulseCounterVolatile = PulseCounter = PULSECOUNTLASTMINUTEMARK = savedCounter;
  attachInterrupt(INPUTPIN, pulseCounter, RISING);
  Timer1.initialize(XMIT_Interval * 1000L);
  Timer1.attachInterrupt(XMIT);
  
  #ifdef SERIAL_EN
    Serial.begin(SERIAL_BAUD);
    DEBUGln("\nTransmitting...");
  #endif
}

void XMIT(void)
{
  noInterrupts();
  PulseCounter = PulseCounterVolatile;
  interrupts();
  
  if (millis() - TIMESTAMP_pulse_curr >= 5000)
  {  ledState = !ledState; digitalWrite(LED, ledState); }

  //calculate Gallons counter 
  GAL = ((float)PulseCounter)/PulsesPerGAL;
  
  DEBUG("PulseCounter:");DEBUG(PulseCounter);DEBUG(", GAL: "); DEBUGln(GAL);
  
  String tempstr = String("GAL:");
  tempstr += (unsigned long)GAL;
  tempstr += '.';
  tempstr += ((unsigned long)(GAL * 100)) % 100;

  //calculate & output GPM
  GPM = pulseAVGInterval > 0 ? 60.0 * 1000 * (1.0/PulsesPerGAL)/(pulseAVGInterval/pulsesPerXMITperiod)
                             : 0;

  pulsesPerXMITperiod = 0;
  pulseAVGInterval = 0;
    
  tempstr += " GPM:";
  tempstr += (int)GPM;
  tempstr += '.';
  tempstr += ((int)(GPM * 100)) % 100;
  
  secondCounter += XMIT_Interval/1000;
  //once per minute, output a GallonsLastMinute count
  if (secondCounter>=60)
  {
    secondCounter=0;
    tempstr += " GLM:";
    float GLM = ((float)(PulseCounter - PULSECOUNTLASTMINUTEMARK))/PulsesPerGAL;
    tempstr += (int)GLM;
    tempstr += '.';
    tempstr += ((int)(GLM * 100)) % 100;
    PULSECOUNTLASTMINUTEMARK = PulseCounter;
    EEPROM_Write_Counter(PulseCounter);
  }

  tempstr.toCharArray(sendBuf, RF12_MAXDATA);
  sendLen = tempstr.length();
  radio.Wakeup();
  radio.Send(GATEWAYID, sendBuf, sendLen);
  radio.Sleep();
  
  DEBUGln(tempstr);
}

void loop() {}

void pulseCounter(void)
{
  noInterrupts();
  ledState = !ledState;
  PulseCounterVolatile++;  // increase when LED turns on
  digitalWrite(LED, ledState);
  NOW = millis();
  
  //remember how long between pulses (sliding window)
  TIMESTAMP_pulse_prev = TIMESTAMP_pulse_curr;
  TIMESTAMP_pulse_curr = NOW;
  
  if (TIMESTAMP_pulse_curr - TIMESTAMP_pulse_prev > GPMthreshold)
    //more than 'GPMthreshold' seconds passed since last pulse... resetting GPM
    pulsesPerXMITperiod=pulseAVGInterval=0;
  else
  {
    pulsesPerXMITperiod++;
    pulseAVGInterval += TIMESTAMP_pulse_curr - TIMESTAMP_pulse_prev;
  }
  
  interrupts();
}

unsigned long EEPROM_Read_Counter()
{
  return EEPROM_Read_ULong(EEPROM_Read_ULong(COUNTERADDR));
}

void EEPROM_Write_Counter(unsigned long counterNow)
{
  if (counterNow == EEPROM_Read_Counter())
  {
    DEBUG("{EEPROM-SKIP(no changes)}");
    return; //skip if nothing changed
  }
  
  DEBUG("{EEPROM-SAVE(");
  DEBUG(EEPROM_Read_ULong(COUNTERADDR));
  DEBUG(")=");
  DEBUG(PulseCounter);
  DEBUG("}");
    
  unsigned long CounterAddr = EEPROM_Read_ULong(COUNTERADDR);
  if (CounterAddr == COUNTERADDRBASE+8*(COUNTEREEPROMSLOTS-1))
    CounterAddr = COUNTERADDRBASE;
  else CounterAddr += 8;
  
  EEPROM_Write_ULong(CounterAddr, counterNow);
  EEPROM_Write_ULong(COUNTERADDR, CounterAddr);
}

unsigned long EEPROM_Read_ULong(int address)
{
  unsigned long temp;
  for (byte i=0; i<8; i++)
    temp = (temp << 8) + EEPROM.read(address++);
  return temp;
}

void EEPROM_Write_ULong(int address, unsigned long data)
{
  for (byte i=0; i<8; i++)
  {
    EEPROM.write(address+7-i, data);
    data = data >> 8;
  }
}
