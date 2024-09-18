#include <Arduino.h>

// this pin should connect to Ground when want to stop the motor
#define STOPPER_PIN 4

// Motor steps per revolution. Most steppers are 200 steps or 1.8 degrees/step
#define MOTOR_STEPS 400
#define RPM 60
// Acceleration and deceleration values are always in FULL steps / s^2
#define MOTOR_ACCEL 2000
#define MOTOR_DECEL 1000

// Microstepping mode. If you hardwired it to save pins, set to the same value here.
#define MICROSTEPS 8

#define DIR 10
#define STEP 11
#define ENABLE A3 // optional (just delete ENABLE from everywhere if not used)

#define SLEEP 12
#define RESET 13

#include "A4988.h"
#define MS1 A2
#define MS2 A1
#define MS3 A0
A4988 stepper(MOTOR_STEPS, DIR, STEP, ENABLE, MS1, MS2, MS3);


#include <ams_as5048b.h>

//unit consts
#define U_RAW 1
#define U_TRN 2
#define U_DEG 3
#define U_RAD 4
#define U_GRAD 5
#define U_MOA 6
#define U_SOA 7
#define U_MILNATO 8
#define U_MILSE 9
#define U_MILRU 10

AMS_AS5048B mysensor;


#define DOOR_OPEN   3
#define DOOR_CLOSE  2

#define STATE_IDLE  0
#define STATE_OPEN  1
#define STATE_CLOSE 2
int state = STATE_IDLE;
int angle_prev = 0;
int angle = 0;
int angle_steps = 0;

void setup() {
    Serial.begin(115200);

    pinMode(SLEEP, OUTPUT);
    pinMode(RESET, OUTPUT);
    digitalWrite(SLEEP, HIGH);
    digitalWrite(RESET, HIGH);

    // Configure stopper pin to read HIGH unless grounded
    pinMode(STOPPER_PIN, INPUT_PULLUP);

    stepper.begin(RPM, MICROSTEPS);
    stepper.disable();

    //stepper.setSpeedProfile(stepper.LINEAR_SPEED, MOTOR_ACCEL, MOTOR_DECEL);
    stepper.setSpeedProfile(stepper.CONSTANT_SPEED, MOTOR_ACCEL, MOTOR_DECEL);

    //init AMS_AS5048B object
    mysensor.begin();

    pinMode(DOOR_OPEN, INPUT);
    pinMode(DOOR_CLOSE, INPUT);

    Serial.println("START");

    stepper.setEnableActiveState(LOW);
//    stepper.enable();
//    stepper.rotate(360);
//    Serial.println("START2");

    if (digitalRead(DOOR_OPEN) == HIGH && state == STATE_IDLE){
      state = STATE_OPEN;
      Serial.println("RECEIVED OPEN");
      stepper.enable();
      stepper.startRotate(-900);
    }
    if (digitalRead(DOOR_CLOSE) == HIGH && state == STATE_IDLE){
      state = STATE_CLOSE;
      Serial.println("RECEIVED CLOSE");
      stepper.enable();
      stepper.startRotate(900);
    }

}

void loop() {
    static int step = 0;

    if(state != STATE_IDLE && millis()%50 == 0){
      angle_prev = angle;
      angle = mysensor.angleR(U_DEG, true);
      angle_steps++;
        Serial.print("Time : ");
        Serial.print(millis(), DEC);
        Serial.print("    Angle : ");
        Serial.print(angle, DEC);
        Serial.print("  angle prev : ");
        Serial.println(angle_prev, DEC);

      if(angle_steps > 5 && (angle_prev - angle) >= -5 && (angle_prev - angle) <= 5){
          Serial.println("STOPPER REACHED");
          stepper.stop();
          stepper.disable();
  
          state = STATE_IDLE;
          step = 0;
          angle_steps = 0;
          delay(10000);
          Serial.println("FINISHED");
      }
    }

    // motor control loop - send pulse and return how long to wait until next pulse
    unsigned wait_time = stepper.nextAction();
//  Serial.println(wait_time);
    step++;

    // 0 wait time indicates the motor has stopped
    if (state != STATE_IDLE && wait_time <= 0) {
        stepper.disable();       // comment out to keep motor powered
        Serial.println("END");
        state = STATE_IDLE;
        step = 0;
        angle_steps = 0;
        delay(10000);
        Serial.println("FINISHED");
    }
}
