/* Realtime Repitch and AutoTune of Audio, by Robert Smith (@RobSmithDev)
 * https://youtube.com/c/robsmithdev
 *
 * See the video at https://youtu.be/n1wGzWAywdk
 * YIN (Ashok Fernandez) Github Repo: https://github.com/ashokfernandez/Yin-Pitch-Tracking
 * 
 * This is free and unencumbered software released into the public domain.
 * See https://unlicense.org/ for more details
 *
 * Original YIN library from https://github.com/ashokfernandez/Yin-Pitch-Tracking
 */


#include <stdint.h>
#include "Yin.h"

#define AUDIO_INPUT     A5

#define SHAFT_ENCODER_CLK     8
#define SHAFT_ENCODER_DATA    9
#define SHAFT_ENCODER_BUTTON 12

#define RGBLED_BLUE          10
#define RGBLED_RED           11
#define RGBLED_GREEN         13

#define MODE_SWITCH          A0


// Arduino doesn't have a log2 function :(
#define log2(x) (log(x) * M_LOG2E)

Yin yInMethod;

// BUFFER_SIZE MUST be 512
uint8_t processingBuffer[BUFFER_SIZE];
volatile bool bufferRequested = false;
volatile unsigned char playbackSpeed = 128;
volatile char ourSpeed = 0;


void setup() {
  cli();
  // put your setup code here, to run once:
  pinMode(AUDIO_INPUT, INPUT);
  pinMode(0, OUTPUT);
  pinMode(1, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);

  pinMode(SHAFT_ENCODER_CLK, INPUT);
  pinMode(SHAFT_ENCODER_DATA, INPUT);
  pinMode(SHAFT_ENCODER_BUTTON, INPUT_PULLUP);

  pinMode(MODE_SWITCH, INPUT_PULLUP);

  pinMode(RGBLED_RED, OUTPUT);
  pinMode(RGBLED_BLUE, OUTPUT);
  pinMode(RGBLED_GREEN, OUTPUT);

  // Prepare for frequency analysis
  Yin_init(&yInMethod, 0.05f);

  //clear ADCSRA and ADCSRB registers
  ADCSRA = 0;
  ADCSRB = 0;

  ADMUX |= (1 << REFS0); //set reference voltage (AVcc with external capacitor at aref pin)
  ADMUX |= (1 << ADLAR); //left align the ADC value- so we can read highest 8 bits from ADCH register only
  ADMUX |= 5;            // Select A5

  ADCSRA |= (1 << ADPS2) |  (1 << ADPS1); // ADC takes 13 clocks, so with 64 prescaler- 16mHz/32=500kHz/13 = 38khz
  ADCSRA |= (1 << ADATE); //enabble auto trigger
  ADCSRA |= (1 << ADEN); //enable ADC
  ADCSRA |= (1 << ADIE); // Enable Interrupt
  ADCSRA |= (1 << ADSC); //start ADC measurements

  PCMSK0 = bit(PCINT0) | bit(PCINT1) | bit(PCINT4);   // Select which pins we want interrupts from (pcint0=pin 8, pcint1=pin 9, pcint4=pin 12)
  PCMSK1 = 0;
  PCMSK2 = 0;
  PCIFR |= bit(PCIF0);  // Clear that its been triggered
  PCICR = bit(PCIF0);   // enable an interrupt for PCINT0-7
  sei();
}

// When new ADC value ready
ISR(ADC_vect) {
  static uint8_t recordBuffer[BUFFER_SIZE];
  static unsigned short inputPosition = 0;
  static unsigned short outputPosition = 0;
  static unsigned short copyPosition = 65535;

  uint8_t soundLevel = ADCH;

  recordBuffer[inputPosition] = soundLevel;
  inputPosition = (inputPosition + 1) & 511;
  outputPosition += playbackSpeed;

  PORTD = recordBuffer[outputPosition >> 7];

  // Was audio requested for pitch estimation?
  if (bufferRequested) {
    // Reset
    if (copyPosition == 65535) copyPosition = 0;

    // Capture data
    processingBuffer[copyPosition++] = soundLevel;

    // Captured enough?
    if (copyPosition >= BUFFER_SIZE) {
      copyPosition = 65535;
      bufferRequested = false;
    }
  }
}

// noteName can be NULL or a char* with space for 3 characvters + null terminator
float getNearestNoteFrequency(float frequency) {
  // Define the note names
  //static const char* const NoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

  // Calculate the number of semitones from C2 (used as a reference point)
  float nearestSemitoneFromC2f = 12.0 * log2(frequency / 65.41);
  uint16_t nearestSemitoneFromC2 = round(nearestSemitoneFromC2f);

  // How I had the above line set when I was recording my Cher/Believe Karaoke Version
  // uint16_t nearestSemitoneFromC2 = round(nearestSemitoneFromC2f/2)*2;

  // Calculate the detected note
  /*if (noteName) {
    uint8_t noteNumber = nearestSemitoneFromC2 % 12;
    strcpy(noteName, NoteNames[noteNumber]);
    // Add the Octave
    uint8_t octave = 2 + (nearestSemitoneFromC2 / 12);
    char tmp[4];
    itoa(octave, tmp, 10);
    strcat(noteName, tmp);
  }*/

  // Use LEDs to show how in tune you are - noteDif gives us -0.5 to 0.5 of onw in tune we are.
  float noteDiff = nearestSemitoneFromC2 - nearestSemitoneFromC2f;  
  // Red would mean we're singing too low, and blue meaning we're too high.
  analogWrite(RGBLED_BLUE, constrain(round((0.5-noteDiff)*512),0,255));
  analogWrite(RGBLED_RED, constrain(round(noteDiff*512),0,255) );

  return pow(2, nearestSemitoneFromC2 / 12.0f) * 65.41f;
}

// Handle changes on these pins
ISR(PCINT0_vect) {
  if (digitalRead(SHAFT_ENCODER_BUTTON) == LOW) ourSpeed = 0;

  bool clkState = digitalRead(SHAFT_ENCODER_CLK) == HIGH;
  static bool clkLastState = true;

  if (clkState != clkLastState) {
    clkLastState = clkState;

    bool dataState = digitalRead(SHAFT_ENCODER_DATA) == HIGH;

    if (dataState == clkState) {
      // anticlockwise
      if (ourSpeed > -128) ourSpeed--;
    } else {
      // Clockwise
      if (ourSpeed < 127) ourSpeed++;
    }
  }
}

void loop() {
  // Mode switch 
  if (digitalRead(MODE_SWITCH) == HIGH) {
    digitalWrite(RGBLED_GREEN, LOW);
    bufferRequested = true;
    while (bufferRequested) {};
    float frequency = round(Yin_getPitch(&yInMethod, processingBuffer));

    // Was a pitch actually detected?
    if (frequency >= 78) {

      static float newFreq = frequency;
      newFreq = (newFreq + frequency) / 2.0f;
      frequency = newFreq;

      // Work out what note to tune to
      float targetFrequency = getNearestNoteFrequency(round(frequency));

      // Using this, we then adjust the speed value
      playbackSpeed = constrain(round(targetFrequency * 128.0 / frequency) + ourSpeed, 1, 255);

    } else {
      playbackSpeed = constrain(128 + ourSpeed, 1, 255);

      // Turn off to signal not working
      analogWrite(RGBLED_RED, 0);
      analogWrite(RGBLED_BLUE, 0 );

    }
  } else {
    // Makes it go from RED to BLUE (red being low, blue being high)
    digitalWrite(RGBLED_GREEN, HIGH);    
    playbackSpeed = constrain(128 + ourSpeed, 1, 255);    
    analogWrite(RGBLED_RED, 127 - ourSpeed);
    analogWrite(RGBLED_BLUE, 128 + ourSpeed );
  }

}
