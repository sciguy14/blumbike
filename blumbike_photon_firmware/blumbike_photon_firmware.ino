// blum.bike Photon Firmware
// Copyright 2020 Jeremy Blum, Blum Idea Labs, LLC.
// www.jeremyblum.com
// This code is licensed under MIT license (see LICENSE.md for details)
#include "Stepper.h"

// Define Pin Constants
const int ENDSTOP_PIN       = A3; // The switch that triggers when the resistance adjuster stepper is at the end of its range
const int RPM_PIN           = D1; // The output of the inverting comparator w/ hysteresis for measuring bike speed
const int BPM_PIN           = D3; // The output pulses from the Heart rate BPM Polar receiver
const int ONBOARD_LED_PIN   = D7; // The onboard blue LED
const int STEPPER_COIL1_EN  = A0; // Stepper motor coil 1 H-Bridge Enable
const int STEPPER_COIL2_EN  = A1; // Stepper motor coil 2 H-Bridge Enable
const int STEPPER_COIL1_MC1 = D2; // Stepper motor coil 1 H-Bridge switch 1 control
const int STEPPER_COIL1_MC2 = D5; // Stepper motor coil 1 H-Bridge switch 2 control
const int STEPPER_COIL2_MC1 = D4; // Stepper motor coil 2 H-Bridge switch 1 control
const int STEPPER_COIL2_MC2 = D6; // Stepper motor coil 2 H-Bridge switch 2 control

// Configuration Constants
const unsigned int UPDATE_RATE_MS = 1000; // Update and send the new values every 1000 milliseconds (this should not be any lower than 1000)
const unsigned int BPM_INTERRUPTED_THRESHOLD = 2000; // If more than this many milliseconds have elapsed since the last BPM trigger, presume the sensor is not transmitting
const unsigned int SESSION_STARTED_SEQUENTIAL_NON_ZERO_READINGS = 2; // A new session is triggered when this many non-zero RPM readings are found in a row
const unsigned int SESSION_ENDED_SEQUENTIAL_ZERO_READINGS = 6; // A session is ended when this many zero RPM readings are found in a row
const double RPM_MOVEMENT_THRESHOLD = 1.0; // To address floating point errors, set this value as the movement threshold. Anything below this is effectively "zero" movement
const unsigned int ZERO_RESISTANCE_TURNS = 2; // This is the numer of full turns the stepper motor must do to achieve the minimum resistance setting (just barely touching the wheel)
const double ROTATIONS_PER_RESISTANCE_STEP = 0.25; // Every 1/4th turn of the stepper motor is equal to one "resistance step"
const unsigned int DEFAULT_RESISTANCE = 1; // This is the default starting session resistance
const unsigned int MAX_RESISTANCE = 8; // This is the maximum resistance setting
const unsigned int MIN_RESISTANCE = 1; // This is the minimum resistance setting

// Define Math Constants
const double BIKE_WHEEL_DIAMETER = 27.0;            // inches
const double DYNO_WHEEL_DIAMETER = 1.80;            // inches
const double PI                  = 3.14159265359;   // ratio
const double FEET_PER_MILE       = 5280.0;          // ratio
const double INCHES_PER_FOOT     = 12.0;            // ratio
const double MINUTES_PER_HOUR    = 60.0;            // ratio
const double SECONDS_PER_MINUTE  = 60.0;            // ratio
const double MILLIS_PER_SECOND   = 1000.0;          // ratio
const int STEPS_PER_REV = 200;                      // The NEMA-17 Stepper motor has 200 steps/rev

// Computed Constants
const double BIKE_WHEEL_CIRCUMFERENCE_INCHES       = PI * BIKE_WHEEL_DIAMETER;                                          // inches
const double BIKE_WHEEL_CIRCUMFERENCE_FEET         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / INCHES_PER_FOOT;                 // feet
const double BIKE_WHEEL_REVOLUTIONS_PER_MILE       = FEET_PER_MILE / BIKE_WHEEL_CIRCUMFERENCE_FEET;                     // revs / mile
const double DYNO_WHEEL_CIRCUMFERENCE_INCHES       = PI * DYNO_WHEEL_DIAMETER;                                          // inches
const double BIKE_DYNO_CIRCUMFERENCE_RATIO         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / DYNO_WHEEL_CIRCUMFERENCE_INCHES; // rpm multiplier

// Define Cloud Functions
int resistanceUp(String na);
int resistanceDown(String na);

// Global Variables
bool in_session = false;
unsigned long resistance = 0;
unsigned long sequential_non_zero_readings = 0;
unsigned long sequential_zero_readings = 0;
unsigned long timestamp_seconds = 0;
double dyno_rpm = 0.0;
double bike_rpm = 0.0;
double bike_mph = 0.0;
double heart_bpm = 0.0;
unsigned long last_rpm_time = 0;
volatile unsigned long revs = 0;
volatile unsigned long last_beat_time = 0;
volatile unsigned long beat_time = 0;
volatile double interrupt_bpm = 0.0;

// Initialize the stepper library - pass it the Switch control pins
Stepper resistance_dial(STEPS_PER_REV, STEPPER_COIL1_MC1, STEPPER_COIL1_MC2, STEPPER_COIL2_MC1, STEPPER_COIL2_MC2);

void setup(){
    // register the cloud functions
    Particle.function("resistance_up", resistanceUp);
    Particle.function("resistance_down", resistanceDown);

    // Set Pin Directions/Modes
    pinMode(ENDSTOP_PIN, INPUT_PULLUP);
    pinMode(RPM_PIN, INPUT);
    pinMode(BPM_PIN, INPUT);
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    pinMode(STEPPER_COIL1_EN, OUTPUT);
    pinMode(STEPPER_COIL2_EN, OUTPUT);
    digitalWrite(STEPPER_COIL1_EN, LOW);
    digitalWrite(STEPPER_COIL2_EN, LOW);
    digitalWrite(ONBOARD_LED_PIN, LOW);

    // Home the Resistance Stepper
    homeStepper();

    // Adjust the stepper such that the resistance is set to the default starting level
    adjustResistanceAbsolute(DEFAULT_RESISTANCE);

    // Cloud Variables - We don't need these, since we publish the values every second, but it's still useful to have them
    Particle.variable("in_session", in_session);
    Particle.variable("resistance", resistance);
    Particle.variable("dyno_rpm", dyno_rpm);
    Particle.variable("bike_rpm", bike_rpm);
    Particle.variable("bike_mph", bike_mph);
    Particle.variable("heart_bpm", heart_bpm);

    // Notify the cloud that the bike has been turned on
    timestamp_seconds = Time.now(); // Get the current time
    String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"powered_on\"}";
    Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);

    // Initialize the last time values to something correct so it is ready for first use
    last_rpm_time = millis();
    last_beat_time = millis();

    // Attach pin change interrupts
    attachInterrupt(RPM_PIN, rpmInterrupt, FALLING);
    attachInterrupt(BPM_PIN, bpmInterrupt, RISING);
}

void loop() {
    // Update at the requested update rate
    // leverage some of this time to keep the update pulse LED on long enough to see
    delay(200);
    digitalWrite(ONBOARD_LED_PIN, LOW);
    delay(UPDATE_RATE_MS-200);

    // Get the current time
    timestamp_seconds = Time.now();

    // Disable interrupts while we process volatile data
    noInterrupts();

    // RPM tracking
    dyno_rpm =  SECONDS_PER_MINUTE*double(UPDATE_RATE_MS)/double(millis() - last_rpm_time)*double(revs); // Compute the RPMs of the Dyno
    last_rpm_time = millis(); // Reset the rpm clock
    revs = 0; // Reset the rpm counter

    //Manage the condition where heartrate is zero
    if (millis() - last_beat_time > BPM_INTERRUPTED_THRESHOLD) {
        heart_bpm = 0.0;
    } else {
        // Set Heartrate equal to he value of the volatile variable computed in the interrupt
        heart_bpm = interrupt_bpm;
    }

    // Interrupts can now trigger again since we are done with the volatile variable
    interrupts();

    // Compute the current bike speed regardless of whether we are currently in a session, so it is visible in the cloud variable
    bike_rpm = dyno_rpm / BIKE_DYNO_CIRCUMFERENCE_RATIO; // Compute the Bike wheel RPM from the Dyno RPM
    bike_mph = (bike_rpm / BIKE_WHEEL_REVOLUTIONS_PER_MILE) * MINUTES_PER_HOUR; // Convert RPM to MPH based on physical constants

    // Constuct a message with the info we care about and send it up to the cloud if we are currently in an active session
    if (in_session) {
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"new_data\", \"bike_mph\": " + String(bike_mph) + ", \"heart_bpm\": " + String(heart_bpm) + "}";
        Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);
    }

    // Get the current time again since above command could be blocking if cloud is unresponsive
    timestamp_seconds = Time.now();

    // If we are not currently in a session, look for movement to see if we should start one
    if (!in_session) {
        if (dyno_rpm >= RPM_MOVEMENT_THRESHOLD) {
            sequential_non_zero_readings = sequential_non_zero_readings + 1;
            if (sequential_non_zero_readings >= SESSION_STARTED_SEQUENTIAL_NON_ZERO_READINGS) {
                in_session = true;
                sequential_non_zero_readings = 0; // Reset variable so its ready for next time

                String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"start_session\"}";
                Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);
            }
        } else {
            sequential_non_zero_readings = 0; // Reset variable if readings are not sequential
        }
    // If we are currently in a session, check to see if movement has stopped, so we can end the session
    } else if (in_session) {
        if (dyno_rpm < RPM_MOVEMENT_THRESHOLD) {
            sequential_zero_readings = sequential_zero_readings + 1;
            if (sequential_zero_readings >= SESSION_ENDED_SEQUENTIAL_ZERO_READINGS) {
                in_session = false;
                sequential_zero_readings = 0; // Reset variable so its ready for next time

                String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"end_session\"}";
                Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);
            }
        } else {
            sequential_zero_readings = 0; // Reset variable if readings are not sequential
        }
    }
}

// This interrupt triggers every time the dyno does a rotation
void rpmInterrupt()
{
    revs = revs + 1;
}

// This interrupt triggers everytime we receive a heartbeat signal
void bpmInterrupt()
{
    beat_time = millis();
    interrupt_bpm = SECONDS_PER_MINUTE/((beat_time-last_beat_time)/MILLIS_PER_SECOND); // Compute the BPM from the heartrate sensor
    last_beat_time = beat_time; // Reset the beat timer
}

void homeStepper()
{
    // Set the speed for the stepper motor
    resistance_dial.setSpeed(60); // 60 RPM

    // Energize the Coils
    digitalWrite(STEPPER_COIL1_EN, HIGH);
    digitalWrite(STEPPER_COIL2_EN, HIGH);
    delay(10);

    // If the end stop pin is already depressed, we back off until it isn't and then rehome so we are as consistent as possible
    if (digitalRead(ENDSTOP_PIN) == LOW)
    {
        while (digitalRead(ENDSTOP_PIN) == LOW)
        {
            resistance_dial.step(-STEPS_PER_REV/8); // Raise one eighth turn at a time
            delay(10);
        }
        resistance_dial.step(-STEPS_PER_REV/8); // Raise a bit more
        delay(100);
    }
    while (digitalRead(ENDSTOP_PIN) == HIGH)
    {
        resistance_dial.step(STEPS_PER_REV/8); // Lower one eighth turn at a time
        delay(10);
    }
    // The Stepper should now be "homed" to the lowest position with the endstop switch depressed

    // Raise to the zero resistance point
    resistance_dial.step(-STEPS_PER_REV*ZERO_RESISTANCE_TURNS);
    resistance = 0;

    // De-energize the Coils because the stepper gets super hot
    digitalWrite(STEPPER_COIL1_EN, LOW);
    digitalWrite(STEPPER_COIL2_EN, LOW);
}

// Functions for Adjusting the Dyno resistance
void adjustResistanceRelative(int levels)
{
    // Bound us to the min/max
    if (resistance + levels > MAX_RESISTANCE) levels = MAX_RESISTANCE - resistance;
    if (resistance + levels < MIN_RESISTANCE) levels = MIN_RESISTANCE - resistance;

    resistance_dial.setSpeed(60); // 60 RPM

    if (digitalRead(ENDSTOP_PIN) == HIGH)
    {
        // Energize the coils
        digitalWrite(STEPPER_COIL1_EN, HIGH);
        digitalWrite(STEPPER_COIL2_EN, HIGH);
        delay(10);

        // Move the Motor
        resistance_dial.step(int(-1.0*double(STEPS_PER_REV)*ROTATIONS_PER_RESISTANCE_STEP*double(levels)));
        resistance = resistance + levels;

        // De-energize the Coils because the stepper gets super hot
        digitalWrite(STEPPER_COIL1_EN, LOW);
        digitalWrite(STEPPER_COIL2_EN, LOW);
    }
}
void adjustResistanceAbsolute(unsigned int level)
{
    adjustResistanceRelative(level-resistance);
}

// Cloud functions that are called upon a matching POST request
int resistanceUp(String na)
{
    adjustResistanceRelative(1);
    return resistance;
}
int resistanceDown(String na)
{
    adjustResistanceRelative(-1);
    return resistance;
}
