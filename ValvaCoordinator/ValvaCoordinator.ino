/*
 * Author:    Dylan Trafford
 * Editted By: Jared Kamp
 * Purpose:   Occams Primary Payload Control and Termination Code
 * Written for MSGC BOREALIS
 * Created:   November 11, 2015
 * Last Edit: April 5, 2017
 * 
 * Environment:       Default Libraries, Arduino 1.6.12
 * Hardware Drivers:  Programmed using Breadboard 1.6.x Hardware settings (Atmega328p 8 MHz internal)
 *                    AVR Pocket Programmer - USBtiny Programmer over ISP interface
 */


/*
 * Note 1: Signed integer max size is 16 bit value; 2^16/2 - 1 = 32767 (to -32,768)
 * Note 2: Since Timer 1 is in use, pwm functionality is not available without overriding timer
 * Note 3: Tone function removes pwm functionality (which we aren't using); Min Frequency = 31 Hz / Max Frequency = 65535 Hz
 * Note 4: Defines do not take up program space as they are handled by the preassembler as a string replace (preferred to constants)
 * Note 5: Playing tunes (like Imperial March) in the main loop, will be interrupted by the ISR1 which occationally will distort sounds (ISR1 takes priority)
 * Note 6: Remember operation precedence in if-statments http://en.cppreference.com/w/c/language/operator_precedence
 */

//Libraries
#include <EEPROM.h>                           //For accessing EEPROM portion of memory

//Defines (string replace)
#ifndef F_CPU
  #define F_CPU 16000000                       //Setting F_CPU for library correct timing as a precaution (16MHz)
#endif

/* 
 *  Note definitions and related commends exert from 
 *  http://www.instructables.com/id/How-to-easily-play-music-with-buzzer-on-arduino-Th/?ALLSTEP
 *  Author: eserra
 * 
 *  NB: ALL NOTES DEFINED WITH STANDARD ENGLISH NAMES, EXCEPT FROM "A" 
 *  THAT IS CALLED WITH THE ITALIAN NAME "LA" BECAUSE A0,A1...ARE THE ANALOG PINS ON ARDUINO.
 *  (Ab IS CALLED Ab AND NOT LAb)
 */
#define F3  174.61
#define Gb3 185.00
#define G3  196.00
#define Ab3 207.65
#define LA3 220.00
#define Bb3 233.08
#define B3  246.94
#define C4  261.63
#define Db4 277.18
#define D4  293.66
#define Eb4 311.13
#define E4  329.63
#define F4  349.23
#define Gb4 369.99
#define G4  392.00
#define Ab4 415.30
#define LA4 440.00
#define Bb4 466.16
#define B4  493.88
#define C5  523.25
// DURATION OF THE NOTES 
#define BPM 120                   // you can change this value changing all the others
#define H 2*Q                     // half 2/4
#define Q 60000/BPM               // quarter 1/4 
#define E Q/2                     // eighth 1/8
#define S Q/4                     // sixteenth 1/16
#define W 4*Q                     // whole 4*4 [editted comment]
//End of eserra authorship


/* Connection To Iridium:
 *  A0,A1,A2,A3,D2,D3,D4,GND
 * |  OUTPUTS  | INPUTS |GND| */

//To Iridium (Status bits to Iridium)
#define iOutput0 A0
#define iOutput1 A1
#define iOutput2 A2
#define iOutput3 A3

//From Iridium (Commands from Iridium)
#define iInput0 2
#define iInput1 3
#define iInput2 4

//Heartbeat Indicator
#define heartbeat 13                          //Digital Pin connected to SCK LED

//Cutdown Indicator and Driver
volatile int timelog = 0;                     //Tracks how long wired cutdown has been running
const int maxtriggeredtime = 20;           //Max runtime duration of timer triggered
#define openValve 10                             //Digital pin connected to MOSFET gate of cutdown
#define closeValve 9

//Timer Backup and Trigger
#define useTimer true                         //If true, will provide a backup timer for wireless primary and secondary cutdown
#define timer_addr 0                          //EEPROM address, bottom of memory is addr 0
const int timer_overflow = 18000;             //When backup timer will trigger in seconds (14400s = 4 hrs)
const int interrupt_counter = 15624;
#define slide_switch 7                        //Switch to control timer reset/servo movement
#define jumper 11                             //Shunt jumper pin at MOSI (jumper from gnd or 5V)

//Audible Indicator
#define speaker 6

//Flags
volatile uint16_t flags = 0;                  //Note: arduino int = 16 bit
const uint16_t valveOpen_mask    = 0b0000000000000010;     //mask to target secondary cutdown flag
const uint16_t valveClose_mask   = 0b0000000000000101;    //mask to target secondary cutdown flag
const uint16_t valveCycle_mask   = 0b0000000000000111;
const uint16_t warning_flag      = 0b0000000000010000;  //mask to target the warning flag

//Prototype Functions
void valveOpen(void);
void valveClose(void);
void valveCycle(void);
void armegeddon(void);
void updateIridium(void);
void tick(void);
void imperialMarch(void);

const bool debugMode = false; //true enables debugging messages

// beeps message codes on audiable speaker for debugging
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
  //Serial Initialization for XBEE Communication:
  Serial.begin(9600);                           //Initialize Serial Com with baudrate
  Serial.setTimeout(5000);                      //Set recieve timeout in milliseconds
  while(Serial.available() > 0){
      Serial.read();                            //Empty any characters in buffer (prevents multiple commands from queuing)
  }
  //Initialize GPIO:
  pinMode(iOutput0, OUTPUT);
  pinMode(iOutput1, OUTPUT);
  pinMode(iOutput2, OUTPUT);
  pinMode(iOutput3, OUTPUT);
  digitalWrite(iOutput0,LOW);                   //Default Low (0)
  digitalWrite(iOutput1,LOW);                   //Default Low (0)
  digitalWrite(iOutput2,LOW);                   //Default Low (0)
  digitalWrite(iOutput3,LOW);                   //Default Low (0)
  pinMode(iInput0, INPUT);
  pinMode(iInput1, INPUT);
  pinMode(iInput2, INPUT);
  pinMode(jumper,INPUT);
  pinMode(heartbeat,OUTPUT);
  digitalWrite(heartbeat,LOW);                  //Default Low (0)
  pinMode(speaker,OUTPUT);
  digitalWrite(speaker,LOW);                    //Default Low (0)
  /*
 * If recently programmed, EEPROM will be all ones at bootup. The chances of this happening naturally and occuring at a bootup 
 * are highly unlikely making this a safe process. This safety feature can be removed if it is a concern but
 * it will require a fuse bit change to protect EEPROM during programming.
 */    
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
  //GetIridiumValue
  int i0 = digitalRead(iInput0);                //Read iInput0 pin2
  int i1 = digitalRead(iInput1);                //Read iInput1 pin3
  int i2 = digitalRead(iInput2);                //Read iInput2 pin4
  int input = (i2*100)+(i1*10)+i0;              //Create (iInput1) concat (iInput2) concat (iInput3)
  long temptime = millis();                     //Save runtime to a temp variable
  switch(input){                                //Case statement based on iInputs
    case 0:   //000
      //System Idle
      break;
      
    case 10:  //010
      valveOpen();                               //Wireless Valve Open
      break;
      
    case 101: //101
      valveClose();
      break;

    case 111: //111
      valveCycle();
      break;
      
    default:
      //Default Case
      break;
  }
  updateIridium();                              //Update Iridium Status bits
 
  if(flags & warning_flag){                     //Read warning indicator flag
    flags &= ~warning_flag;                     //warning flag was set, now clear it
  }
  delay(50);                                    //Short Delay of 50ms (if removed Serial tranmissions and recieves malfunction)
}

ISR(TIMER1_COMPA_vect)                          //1 Hz interrupt
{
  //Toggle LED
  digitalWrite(heartbeat,!digitalRead(heartbeat));
  tick();

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
//      armegeddon();
      flags |= warning_flag;                    //Set warning flag
    }
    else{
      flags &= ~warning_flag;                   //clear warning flag
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

void updateIridium(void){

  /*  Iridium Translation:
   *  I0 = nichrome wire cutdown triggered
   *  I1 = valve open triggered
   *  I2 = valve close triggered
   *  I3 = fan on or off triggered
   */ 
  
  if((flags & valveOpen_mask)!=0){digitalWrite(iOutput1,HIGH);}    //if the flag is low, statement is true
  else{digitalWrite(iOutput1,LOW);}
  
  if((flags & valveClose_mask)!=0){digitalWrite(iOutput2,HIGH);}   //if the flag is low, statement is true
  else{digitalWrite(iOutput2,LOW);}

}

//Valve Open Wireless Request
void valveOpen(void){
  Serial.write("O");
  char confirm = 'N';
  while(Serial.available() > 0){
    confirm = (char)Serial.read();
    Serial.write(confirm);
  }
  if(confirm == 'Y'){
    flags |= valveOpen_mask;                     //Set flag that request was confirmed
  }
  if(debugMode){
    message(2);
  }
}

//Valve Close Wireless Request
void valveClose(void){
  Serial.write("C");
  char confirm = 'N';
  while(Serial.available() > 0){
    confirm = (char)Serial.read();
    Serial.write(confirm);
  }
  if(confirm == 'Y'){
    flags |= valveClose_mask;                     //Set flag that request was confirmed
  }
  if(debugMode){
    message(3);
  }
}

void valveCycle(void){
    Serial.write("S");
    char confirm = 'N';
    while(Serial.available() > 0){
      confirm = (char)Serial.read();
      Serial.write(confirm);
    }
    if(confirm == 'Y'){
      flags |= valveCycle_mask;
    }
    if(debugMode){

    }
}

//The goal of this function is to bring down the balloon by any means available. This function is for a worst case situation.
void armegeddon(void){
  valveOpen();
  imperialMarch();
}

void tick(void){
  if(!debugMode){                             //No heartbeat during debugging to avoid conflicts with debug messages.
    tone(speaker,C5,S/4);                         //Play a short pip (1/64 of a beat at C 5th octive)
  }
}

void imperialMarch(void){
    //Code adapted from http://pasted.co/e525c1b2 and eserra @ http://www.instructables.com/id/How-to-easily-play-music-with-buzzer-on-arduino-Th/?ALLSTEPS
    //tone(pin, note, duration)
    tone(speaker,LA3,Q); 
    delay(1+Q); //delay duration should always be 1 ms more than the note in order to separate them.
    tone(speaker,LA3,Q);
    delay(1+Q);
    tone(speaker,LA3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    
    tone(speaker,LA3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,LA3,H);
    delay(1+H);
    
    tone(speaker,E4,Q); 
    delay(1+Q); 
    tone(speaker,E4,Q);
    delay(1+Q);
    tone(speaker,E4,Q);
    delay(1+Q);
    tone(speaker,F4,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    
    tone(speaker,Ab3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,LA3,H);
    delay(1+H);
    
    tone(speaker,LA4,Q);
    delay(1+Q);
    tone(speaker,LA3,E+S);
    delay(1+E+S);
    tone(speaker,LA3,S);
    delay(1+S);
    tone(speaker,LA4,Q);
    delay(1+Q);
    tone(speaker,Ab4,E+S);
    delay(1+E+S);
    tone(speaker,G4,S);
    delay(1+S);
    
    tone(speaker,Gb4,S);
    delay(1+S);
    tone(speaker,E4,S);
    delay(1+S);
    tone(speaker,F4,E);
    delay(1+E);
    delay(1+E);//PAUSE
    tone(speaker,Bb3,E);
    delay(1+E);
    tone(speaker,Eb4,Q);
    delay(1+Q);
    tone(speaker,D4,E+S);
    delay(1+E+S);
    tone(speaker,Db4,S);
    delay(1+S);
    
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,B3,S);
    delay(1+S);
    tone(speaker,C4,E);
    delay(1+E);
    delay(1+E);//PAUSE QUASI FINE RIGA
    tone(speaker,F3,E);
    delay(1+E);
    tone(speaker,Ab3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,LA3,S);
    delay(1+S);
    
    tone(speaker,C4,Q);
    delay(1+Q);
     tone(speaker,LA3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,E4,H);
    delay(1+H);
    
    tone(speaker,LA4,Q);
    delay(1+Q);
    tone(speaker,LA3,E+S);
    delay(1+E+S);
    tone(speaker,LA3,S);
    delay(1+S);
    tone(speaker,LA4,Q);
    delay(1+Q);
    tone(speaker,Ab4,E+S);
    delay(1+E+S);
    tone(speaker,G4,S);
    delay(1+S);
    
    tone(speaker,Gb4,S);
    delay(1+S);
    tone(speaker,E4,S);
    delay(1+S);
    tone(speaker,F4,E);
    delay(1+E);
    delay(1+E);//PAUSE
    tone(speaker,Bb3,E);
    delay(1+E);
    tone(speaker,Eb4,Q);
    delay(1+Q);
    tone(speaker,D4,E+S);
    delay(1+E+S);
    tone(speaker,Db4,S);
    delay(1+S);
    
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,B3,S);
    delay(1+S);
    tone(speaker,C4,E);
    delay(1+E);
    delay(1+E);//PAUSE QUASI FINE RIGA
    tone(speaker,F3,E);
    delay(1+E);
    tone(speaker,Ab3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    
    tone(speaker,LA3,Q);
    delay(1+Q);
    tone(speaker,F3,E+S);
    delay(1+E+S);
    tone(speaker,C4,S);
    delay(1+S);
    tone(speaker,LA3,H);
    delay(1+H);
    
    delay(2*H);
}

