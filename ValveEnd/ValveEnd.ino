/*
 * Author:    Trevor Gahl
 * Editted By: Jared Kamp
 * Purpose:   Occams End Device
 * Written for MSGC BOREALIS
 * Created:   April 2, 2017
 * Last Edit: April 2, 2017
 *
 * Environment:       Default Libraries, Arduino 1.6.12
 * Hardware Drivers:  Programmed using Breadboard 1.6.x Hardware settings (Atmega328p 8 MHz internal)
 *                    AVR Pocket Programmer - USBtiny Programmer over ISP interface
 */
#include <Wire.h>
#include <EEPROM.h>                           //For accessing EEPROM portion of memory

#ifndef F_CPU
#define F_CPU 16000000                       //Setting F_CPU for library correct timing as a precaution (16MHz)
#endif
#define speaker 6

volatile int test;
volatile int count;
char incoming;
char lastincome;
bool state = false;   //is valve open
const char openvalve = 'O';
const char closevalve = 'C';
const char valvecycle = 'S';

#define F3  174.61
#define Ab3 207.65
#define C5  523.25
#define BPM 120                   // you can change this value changing all the others
#define Q 60000/BPM               // quarter 1/4
#define E Q/2                     // eighth 1/8
#define S Q/4                     // sixteenth 1/16

//Heartbeat Indicator
#define heartbeat 13                          //Digital Pin connected to SCK LED

//Cutdown Indicator and Driver
volatile int timelog = 0;                     //Tracks how long wired cutdown has been running
const int maxtriggeredtime = 20;           //Max runtime duration of timer triggered
#define openValve 10                            //Digital pin connected to MOSFET gate of cutdown
#define closeValve 9

//Timer Backup and Trigger
#define useTimer true                         //If true, will provide a backup timer for wireless primary and secondary cutdown
#define timer_addr 0                          //EEPROM address, bottom of memory is addr 0
const int timer_overflow = 18000;             //When backup timer will trigger in seconds (14400s = 4 hrs)
const int interrupt_counter = 15624;
#define slide_switch 7                        //Switch to control timer reset/servo movement
#define jumper 11                             //Shunt jumper pin at MOSI (jumper from gnd or 5V)

const bool debugMode = false;          //true if debugging

// beeps message codes on audiable speaker
void message(uint8_t errno) {
  uint8_t i;
  for (i=0; i<errno; i++) {
    tone(speaker,F3*(1+i),E);
    tone(speaker,Ab3*(1+i),S);
    delay(550);
  }
delay(3000);
}

void setup() {
  // put your setup code here, to run once:
  Wire.begin(2);
  Wire.onRequest(requestEvent);
  //Serial Initialization for XBEE Communication:
  Serial.begin(9600);                           //Initialize Serial Com with baudrate
  Serial.setTimeout(1000);                      //Set recieve timeout in milliseconds
  while(Serial.available() > 0){
      Serial.read();
  }

  incoming = '0'; //Initialize array to ascii code zeros
  pinMode(jumper,INPUT);
  pinMode(heartbeat,OUTPUT);
  digitalWrite(heartbeat,LOW);                  //Default Low (0)
  pinMode(speaker,OUTPUT);
  digitalWrite(speaker,LOW);                    //Default Low (0)
  pinMode(openValve,OUTPUT);
  digitalWrite(openValve,LOW);                    //Default Low (0)
  pinMode(closeValve, OUTPUT);
  digitalWrite(closeValve, LOW);

  //Timer Initialization:
  byte test = 0x00;
  byte test1 = 0x00;
  int timer = 0;
  EEPROM.get(timer_addr,test);
  EEPROM.get(timer_addr+1,test1);
  if((test == 0xFF && test1 == 0xFF)|(EEPROM.get(timer_addr,timer) > timer_overflow+maxtriggeredtime)){
    int zero = 0;
    EEPROM.put(timer_addr, zero);
  }

  //Interrupt Initialization (Timer1):
  cli();                                        //disable global interrupts
  TCCR1A = 0;                                   //set entire TCCR1A register to 0
  TCCR1B = 0;                                   //same for TCCR1B
  OCR1A = interrupt_counter;                                //set compare match register to desired timer count
  TCCR1B |= (1 << WGM12);                       //turn on CTC mode
  TCCR1B |= (1 << CS10);                        //Set CS10 and CS11 bits for 1028 prescaler
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (1 << OCIE1A);                      //enable timer compare interrupt
  sei();                                        //enable global interrupts
}

void loop() {
  long temptime = millis();                     //Save runtime to a temp variable
  if(first_cycle == true){
    while(Serial.available() > 0){                                    //Need to let the buffer build up between reads
      incoming = Serial.read();                     //Grab command from XBEE
      incoming = Serial.read();                     //Grab command from XBEE
      first_cycle = false;
    }
  }else{
      while(Serial.available() > 0){                                    //Need to let the buffer build up between reads
        incoming = Serial.read();                     //Grab command from XBEE
      }

  }
  if(incoming == openvalve){  //Reset incoming to defaults
    lastincome = incoming;
    incoming = '0';

    if (state == false){
      digitalWrite(openValve, HIGH);
      delay(102320);
      digitalWrite(openValve, LOW);
      state = true;

      if(debugMode){
      message(2);
      }
      while(Serial.available() > 0){+
      Serial.read();                            //Empty any characters in buffer (prevents multiple commands from queuing)
      }
   }else{
    digitalWrite(openValve,LOW);

    while(Serial.available() > 0){+
      Serial.read();                            //Empty any characters in buffer (prevents multiple commands from queuing)
    }
   }

  }else if(incoming == closevalve){
    lastincome = incoming;
    incoming = '0';

    if (state == true){
      digitalWrite(closeValve, HIGH);
      delay(102320);
      digitalWrite(closeValve, LOW);
      state = false;

      if(debugMode){
      message(3);
      }
      while(Serial.available() > 0){
      Serial.read();                            //Empty any characters in buffer (prevents multiple commands from queuing)
      }

    }else{
      digitalWrite(closeValve, LOW);

      while(Serial.available() > 0){
      Serial.read();                            //Empty any characters in buffer (prevents multiple commands from queuing)
      }
    }
  }else if(incoming == valvecycle){
    lastincome = incoming;
    incoming = '0';

    if (state == false){
      digitalWrite(openValve, HIGH);
      delay(6000);
      digitalWrite(openValve, LOW);
      delay(3000);
      digitalWrite(closeValve, HIGH);
      delay(6000);
      digitalWrite(closeValve,LOW);
      if(debugMode){
      message(3);
      }
      while(Serial.available() > 0){
      Serial.read();
      }
    }else if (state == true){
      digitalWrite(closeValve, HIGH);
      delay(6000);
      digitalWrite(closeValve, LOW);
      delay(3000);
      digitalWrite(openValve, HIGH);
      delay(6000);
      digitalWrite(openValve, LOW);
      delay(3000);
      digitalWrite(closeValve, HIGH);
      delay(6000);
      digitalWrite(closeValve,LOW);
      state = false;
    }
  }else{
    //idle
    //loop back and get the next command from serial.
    }
}
ISR(TIMER1_COMPA_vect)                          //1 Hz interrupt
{
  //Toggle LED
  digitalWrite(heartbeat,!digitalRead(heartbeat));
  tone(speaker,C5,S/4);                         //Play a short pip (1/64 of a beat at C 5th octive)

  if(useTimer){
    //Get timer value from EEPROM
    int timer = 0;
    EEPROM.get(timer_addr,timer);
    //Increment timer by interrupt occurance
    timer++;
    //Write Timer to EEPROM for preservation
    EEPROM.put(timer_addr,timer);

    //Timer Trigger and Timeout
    if(timer >= timer_overflow && timer < timer_overflow+maxtriggeredtime){
//    digitalWrite(cutdown,HIGH);                 //Short MOSFET drain and source
    }
    //Poll Timer Disable/Reset Switch
    if(!digitalRead(slide_switch)&&digitalRead(jumper)){      //Redundant, two trigger, timer reset
      timer = 0;                                //Reset timer to zero
      EEPROM.put(timer_addr,timer);             //Save timer to EEPROM
      //Change Heartbeat Flash Rate to indicate switch in timer disable position
      OCR1A = 500;
    }
    else{
      //1Hz Interrupt Counter
      OCR1A = interrupt_counter;
    }
  }
}

void requestEvent(){
  Wire.write(lastincome);
}
