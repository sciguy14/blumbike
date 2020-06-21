// blum.bike Photon Firmware
// Copyright 2020 Jeremy Blum, Blum Idea Labs, LLC.
// www.jeremyblum.com
// This code is licensed under MIT license (see LICENSE.md for details)

// Define compiler replacements for Stepper motor direction
#define HIGHER_RESISTANCE LOW
#define LOWER_RESISTANCE HIGH

// Define compiler replacements for endstop start_session
#define PRESSED LOW
#define UNPRESSED HIGH

// Define Pin Constants
const int ENDSTOP_PIN       = A3; // The switch that triggers when the resistance adjuster stepper is at the end of its range
const int RPM_PIN           = D1; // The output of the inverting comparator w/ hysteresis for measuring bike speed
const int BPM_PIN           = D3; // The output pulses from the Heart rate BPM Polar receiver
const int ONBOARD_LED_PIN   = D7; // The onboard blue LED
const int STEPPER_ENN       = D4; // TMC2209 Stepper Driver Enable Pin (Active Low)
const int STEPPER_STEP      = D5; // TMC2209 Stepper Driver Step Pin
const int STEPPER_DIR       = D6; // TMC2209 Stepper Driver Direction Pin

// Configuration Constants
const unsigned int UPDATE_RATE_MS                               = 1000; // Update and send the new values every 1000 milliseconds (this should not be any lower than 1000)
const unsigned int BPM_INTERRUPTED_THRESHOLD                    = 2000; // If more than this many milliseconds have elapsed since the last BPM trigger, presume the sensor is not transmitting
const unsigned int SESSION_STARTED_SEQUENTIAL_NON_ZERO_READINGS = 2;    // A new session is triggered when this many non-zero RPM readings are found in a row
const unsigned int SESSION_ENDED_SEQUENTIAL_ZERO_READINGS       = 6;    // A session is ended when this many zero RPM readings are found in a row
const double RPM_MOVEMENT_THRESHOLD                             = 1.0;  // To address floating point errors, set this value as the movement threshold. Anything below this is effectively "zero" movement
const double ZERO_RESISTANCE_TURNS                              = 2.75;  // The number of full output shaft turns the stepper motor must do to achieve the minimum resistance setting (just barely touching the wheel)
const double ROTATIONS_PER_RESISTANCE_STEP                      = 0.25; // Every 1/4th turn of the stepper motor output shaft is equal to one "resistance step"
const unsigned int DEFAULT_RESISTANCE                           = 1;    // This is the default starting session resistance
const unsigned int MAX_RESISTANCE                               = 8;    // This is the maximum resistance setting
const unsigned int MIN_RESISTANCE                               = 1;    // This is the minimum resistance setting

// Define Math Constants
const double BIKE_WHEEL_DIAMETER    = 27.0;            // inches
const double DYNO_WHEEL_DIAMETER    = 1.80;            // inches
const double PI                     = 3.14159265359;   // ratio
const double FEET_PER_MILE          = 5280.0;          // ratio
const double INCHES_PER_FOOT        = 12.0;            // ratio
const double MINUTES_PER_HOUR       = 60.0;            // ratio
const double SECONDS_PER_MINUTE     = 60.0;            // ratio
const double MILLIS_PER_SECOND      = 1000.0;          // ratio
const double STEPS_PER_REV_NO_GEAR  = 200.0;           // The NEMA-17 Stepper motor has 200 steps/rev
const double MICROSTEPPING          = 8.0;             // MS1 and MS2 are pulled down by default putting us in 1/8 Microstepping mode
const double GEAR_RATIO             = 5.18;            // The planetary gearbox for the motor is 5.18:1

// Computed Constants
const double BIKE_WHEEL_CIRCUMFERENCE_INCHES       = PI * BIKE_WHEEL_DIAMETER;                                          // inches
const double BIKE_WHEEL_CIRCUMFERENCE_FEET         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / INCHES_PER_FOOT;                 // feet
const double BIKE_WHEEL_REVOLUTIONS_PER_MILE       = FEET_PER_MILE / BIKE_WHEEL_CIRCUMFERENCE_FEET;                     // revs / mile
const double DYNO_WHEEL_CIRCUMFERENCE_INCHES       = PI * DYNO_WHEEL_DIAMETER;                                          // inches
const double BIKE_DYNO_CIRCUMFERENCE_RATIO         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / DYNO_WHEEL_CIRCUMFERENCE_INCHES; // rpm multiplier
const double STEPS_PER_REV                         = STEPS_PER_REV_NO_GEAR * GEAR_RATIO * MICROSTEPPING;                // The actual number of step pulses to go a full rotation of the output shaft

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

void setup(){
    // register the cloud functions
    Particle.function("resistance_up", resistanceUp);
    Particle.function("resistance_down", resistanceDown);

    // Set Pin Directions/Modes
    pinMode(ENDSTOP_PIN, INPUT_PULLUP);
    pinMode(RPM_PIN, INPUT);
    pinMode(BPM_PIN, INPUT);
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    pinMode(STEPPER_ENN, OUTPUT);
    pinMode(STEPPER_STEP, OUTPUT);
    pinMode(STEPPER_DIR, OUTPUT);
    digitalWrite(STEPPER_ENN, HIGH);
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
    // If the endstop pin is already pressed, we back off until it isn't and then rehome so we are as consistent as possible
    if (digitalRead(ENDSTOP_PIN) == PRESSED)
    {
        while (digitalRead(ENDSTOP_PIN) == PRESSED)
        {
            move(HIGHER_RESISTANCE, 0.01); // Raise a little bit at a time
        }
        move(HIGHER_RESISTANCE, 0.25); // Raise another quarter output shaft turn
        delay(100);
    }

    // Move twoards the endstop until it is pressed
    while (digitalRead(ENDSTOP_PIN) == UNPRESSED)
    {
        move(LOWER_RESISTANCE, 0.01); // Lower a little bit at a time
    }
    // The Stepper should now be "homed" to the lowest position with the endstop switch depressed

    delay(1000);

    // Raise to the zero resistance point
    move(HIGHER_RESISTANCE, ZERO_RESISTANCE_TURNS);
    resistance = 0;

    delay(1000);
}

// Functions for Adjusting the Dyno resistance
void adjustResistanceRelative(int levels)
{
    // The endstop should not be pressed unless we fell down a bunch since homing
    if (digitalRead(ENDSTOP_PIN) == PRESSED) homeStepper();

    // Bound us to the min/max
    if (resistance + levels > MAX_RESISTANCE) levels = MAX_RESISTANCE - resistance;
    if (resistance + levels < MIN_RESISTANCE) levels = MIN_RESISTANCE - resistance;

    // Move the Motor
    bool dir = LOWER_RESISTANCE;
    if (levels > 0) dir = HIGHER_RESISTANCE;
    move(dir, double(levels)*ROTATIONS_PER_RESISTANCE_STEP);
    resistance = resistance + levels;
}
void adjustResistanceAbsolute(unsigned int level)
{
    adjustResistanceRelative(level-resistance);
}

// Move the stepper Motor
// Motor is kept disabled when not moving
// This function is blocking
// dir = Direction of rotation
// rotations = Number of output shaft rotations
void move(bool dir, double rotations)
{
    //Set direction and enable motor
    digitalWrite(STEPPER_DIR, dir);
    digitalWrite(STEPPER_ENN, LOW);

    double steps = rotations * STEPS_PER_REV;
    double i = 0.0;
    while (i < steps)
    {
        digitalWrite(STEPPER_STEP, HIGH);
        delayMicroseconds(75); // I experienced FreeRTOS timer issues if making this number too large (more than ~500)
        digitalWrite(STEPPER_STEP, LOW);
        delayMicroseconds(75);
        i = i + 1.0;

    }
    // Disable Motor
    digitalWrite(STEPPER_ENN, HIGH);
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
