// blum.bike Photon Firmware
// Copyright 2020 Jeremy Blum, Blum Idea Labs, LLC.
// www.jeremyblum.com
// This code is licensed under MIT license (see LICENSE.md for details)

// Define Pin Constants
const int RPM_PIN           = 1; // The output of the Inverting Comparator w/ Hysteresis connects to Photon Pin D1
const int BPM_PIN           = 3; // Heart rate BPM output is connected to Photon Pin D3
const int ONBOARD_LED_PIN   = 7; // The onboard Blue LED is connected to Photon Pin D7

// Configuration Constants
const unsigned int UPDATE_RATE_MS = 1000; // Update and send the new values every 1000 milliseconds
const unsigned int BPM_INTERRUPTED_THRESHOLD = 2000; // If more than this many milliseconds have elapsed since the last BPM trigger, presume the sensor is not transmitting

// Define Math Constants
const double BIKE_WHEEL_DIAMETER = 27.0;            // inches
const double DYNO_WHEEL_DIAMETER = 1.80;            // inches
const double PI                  = 3.14159265359;   // ratio
const double FEET_PER_MILE       = 5280.0;          // ratio
const double INCHES_PER_FOOT     = 12.0;            // ratio
const double MINUTES_PER_HOUR    = 60.0;            // ratio
const double SECONDS_PER_MINUTE  = 60.0;            // ratio
const double MILLIS_PER_SECOND   = 1000.0;          // ratio

// Computed Constants
const double BIKE_WHEEL_CIRCUMFERENCE_INCHES       = PI * BIKE_WHEEL_DIAMETER;                                          // inches
const double BIKE_WHEEL_CIRCUMFERENCE_FEET         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / INCHES_PER_FOOT;                 // feet
const double BIKE_WHEEL_REVOLUTIONS_PER_MILE       = FEET_PER_MILE / BIKE_WHEEL_CIRCUMFERENCE_FEET;                     // revs / mile
const double DYNO_WHEEL_CIRCUMFERENCE_INCHES       = PI * DYNO_WHEEL_DIAMETER;                                          // inches
const double BIKE_DYNO_CIRCUMFERENCE_RATIO         = BIKE_WHEEL_CIRCUMFERENCE_INCHES / DYNO_WHEEL_CIRCUMFERENCE_INCHES; // rpm multiplier

// Global Variables
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

void setup()
{
    // Set Pin Directions
    pinMode(RPM_PIN, INPUT);
    pinMode(BPM_PIN, INPUT);
    pinMode(ONBOARD_LED_PIN, OUTPUT);

    // Cloud Variables - WE don't need these, since we publish the values every second, but it's still useful to have them
    Particle.variable("dyno_rpm", dyno_rpm);
    Particle.variable("bike_rpm", bike_rpm);
    Particle.variable("bike_mph", bike_mph);
    Particle.variable("heart_bpm", heart_bpm);

    // Start the Session and notify the cloud
    timestamp_seconds = Time.now(); // Get the current time
    String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"start_session\"}";
    Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);

    // Initialize the last time values to something correct so it is ready for first use
    last_rpm_time = millis();
    last_beat_time = millis();

    // Attach pin change interrupts
    attachInterrupt(RPM_PIN, rpmInterrupt, FALLING);
    attachInterrupt(BPM_PIN, bpmInterrupt, RISING);
}

void loop()
{
    delay(UPDATE_RATE_MS); // Update at the requested update rate

    timestamp_seconds = Time.now(); // Get the current time

    noInterrupts(); // Disable interrupts while we process volatile data

    dyno_rpm =  SECONDS_PER_MINUTE*double(UPDATE_RATE_MS)/double(millis() - last_rpm_time)*double(revs); // Compute the RPMs of the Dyno
    last_rpm_time = millis(); // Reset the rpm clock
    revs = 0; // Reset the rpm counter


    if (millis() - last_beat_time > BPM_INTERRUPTED_THRESHOLD)
    {
        heart_bpm = 0.0;
    }
    else
    {
        heart_bpm = interrupt_bpm; // Set Heartrate equal to he value of the volatile variable computed in the interrupt
    }
    interrupts(); // Interrupts can now trigger again since we are done with the volatile variable

    bike_rpm = dyno_rpm / BIKE_DYNO_CIRCUMFERENCE_RATIO; // Compute the Bike wheel RPM from the Dyno RPM
    bike_mph = (bike_rpm / BIKE_WHEEL_REVOLUTIONS_PER_MILE) * MINUTES_PER_HOUR; // Convert RPM to MPH based on physical constants

    // Constuct a message with the info we care about and send it up to the cloud
    String msg = "{\"t\": " + String(timestamp_seconds) + ", \"event\": \"new_data\", \"bike_mph\": " + String(bike_mph) + ", \"heart_bpm\": " + String(heart_bpm) + "}";
    Particle.publish("bike_data", msg, PRIVATE | WITH_ACK);
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
