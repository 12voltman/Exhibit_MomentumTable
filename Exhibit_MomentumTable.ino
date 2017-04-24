/* Momentum Table Monitoring and Control Program // 
   This does not provide direct motor control, but uses a GS1 Motor drive
   control from AutomationDirect. Control exists through contact 
   closure only at this time. 
*/
 
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h> // added this to fix a weird arduino ide bug
#include "Secrets.h"

//////
#define DESIREDRPM_TABLE    60      //desired RPM on output wheel
//#define DESIREDRPM_MOTOR    600     //desired motor RPM (this may be superfulous) 
#define DISCSLIPCOUNT       250     //How long alarm cond before alarm signal
#define LOCKEDDISCRPM       20     //RPM to consider as "disc stopped"
#define ROTATIONVARIANCE    2      //Variance in RPM before alarm condition 
//#define TRANSMISSIONRATIO   10      //Tranmission drive ratio n:1
#define NUMBEROFRESTARTS    3     //#of restarts attempted after stopped state
#define STARTTIME           10000   //Expected ramp up time, in MS
#define STOPTIME            10000   //Expected ramp down time, in MS
 /////////
#define STARTRELAYPIN       5      //pin locations
#define HALLSENSORPIN       4       //hall sensor to detect rotaion
#define SECONDS             1000    //ms in seconds 

WiFiClient espClient;
PubSubClient client(espClient);



/**************************************/
/*            GLOBAL VARS              /
/**************************************/

uint16_t hallSensor1_count  = 0 ;
uint16_t hallSensor2_count  = 0 ;
volatile uint8_t pulseCount = 0 ;
uint8_t rpm                 = 0 ;
unsigned long timeOld       = 0 ;
uint8_t allStopVar          = 1 ;
//
unsigned long now           = 0 ;
unsigned long prevMillis    = 0 ;
unsigned long OTAUntilMillis = 0 ;
uint8_t discSlipPrecond     = 0 ;
uint8_t restartCount        = 0 ;
char msg[50];                   
uint8_t reconnectAttempt 	= 0;

/**************************************/
/*               SETUP                 /
/**************************************/
void setup(){
pinMode(STARTRELAYPIN, OUTPUT);
pinMode(HALLSENSORPIN, INPUT_PULLUP);
digitalWrite(STARTRELAYPIN, LOW);
attachInterrupt(digitalPinToInterrupt(HALLSENSORPIN), sensorPulseCount, FALLING);
client.setServer(mqtt_server, 1883);
client.setCallback(callback);
Serial.begin(115200);
/////////////////////////////
delay(5000);
motionControlStart(); // remove when MQTT is active. testing only 
/////////////////////////////
}


/**************************************/
/*                LOOP                 /
/**************************************/
void loop(){
	delay(3);
if  (allStopVar == 0){
    calculateRPM();
    discSlipCheck();
    lockedRotorCheck();
}
    now = millis();
    //MQTT Connection Check
	//if (!client.connected()) {
	//	reconnect();
    //}
    //client.loop();
}


/**************************************/
/*             FUNCTIONS               /
/**************************************/
void sensorPulseCount(){
pulseCount++;
// expected two pulse counts per rotation, two sensors 
}

void calculateRPM(){
if (millis() - prevMillis >= 5000){
	Serial.println("CalculatingRPM");
    detachInterrupt(HALLSENSORPIN);              //disable interrupts while running calculate
    rpm = pulseCount * 6;          //converting frequency to RPM(two magnets on disc x 5 sec)
    pulseCount = 0;                 //reset pulse counter
    prevMillis = millis();          //update prevMillis
    Serial.print("RPM: ");
    Serial.println(rpm);
    attachInterrupt(digitalPinToInterrupt(HALLSENSORPIN), sensorPulseCount, FALLING);
    }
  }

void motionControlStart(){
    if (restartCount < NUMBEROFRESTARTS){
	    Serial.println("Motor Control Start");
		digitalWrite(STARTRELAYPIN, HIGH); // enable start relay if not above fail counter
        delay(3000); // wait for 3 second for a rotation 
        if (pulseCount == 0){               // if no rotation detected in interrupt
            digitalWrite(STARTRELAYPIN, LOW); // turn off motor becuase of no rotation
            restartCount++;
            delay (10000); // delay 10 seconds before attempting restart
            pulseCount = 0; // reset pulse count 
            motionControlStart();
            }
        if (pulseCount > 1){
        Serial.println("motion started, system running");
        restartCount = 0; // set restart counter to 0, since we're started
        allStopVar = 0; // Set allstop condition to off 
		Serial.print("AllStopVar ");
		Serial.println(allStopVar);
        }
    }    
    if (restartCount >= NUMBEROFRESTARTS) {
        digitalWrite(STARTRELAYPIN, LOW);
        Serial.println("disc not started");
        client.publish(TOPIC_T, "CritErr: Disc Start / Restart Failed --- SYSTEM HALTED");
        //MQTT "Disc Start / Restart Failed ---- SYSTEM HALTED"
        allStop();   
    }
}

void motionControlStop(){
    digitalWrite(STARTRELAYPIN, LOW);//disable start relay
    delay (10000); // wait 10 seconds for spindown
    calculateRPM();
	Serial.println("MotionControlStop");
    if (rpm > 0){
        client.publish(TOPIC_T, "Error: Disc not stopped even though commanded, System halting");
        allStop();
    }
}    

void discSlipCheck(){
    if (rpm < (DESIREDRPM_TABLE - ROTATIONVARIANCE && allStopVar == 0)){
        discSlipPrecond++;
		Serial.println("Disc is slipping from nominal RPM");
		Serial.println(discSlipPrecond);
        }
    else
        if (rpm >= (DESIREDRPM_TABLE - ROTATIONVARIANCE && allStopVar == 0)){
        discSlipPrecond--;
        }
    if (discSlipPrecond > DISCSLIPCOUNT){
    client.publish(TOPIC_T, "Error: - Continuious Disc Slip Detected"); 
    }
}


void lockedRotorCheck(){
    if (rpm <= LOCKEDDISCRPM){
        digitalWrite(STARTRELAYPIN, LOW); // immediately shut down if RPM lowByte
        client.publish(TOPIC_T, "CritErr: Disc rotation is locked, attempting restart"); 
		Serial.println("Disc motion has been stopped. Attempting Restart");
		delay(10000);
		motionControlStart();
		}                            
		
}

void allStop(){
    digitalWrite(STARTRELAYPIN, LOW);
    Serial.println("System in All-Stop");
    client.publish(TOPIC_T, "CritErr: System halted. All stop active. Restart Required");
    allStopVar = 1;
    delay(10000);
    }    
void exhibitStatusMsg(){
    char gs_msg [50];
    snprintf (gs_msg, 38, "Disc RPM %i", now / rpm);
    client.publish(TOPIC_T, gs_msg);
        }
