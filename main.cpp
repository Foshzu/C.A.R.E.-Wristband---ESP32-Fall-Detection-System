/*=======LIBRARIES=======*/
#include <Wire.h>
#include <MPU6050.h>
#include <HardwareSerial.h>
#include <TFT_eSPI.h>
#include <SPI.h>
/*=======OBJECTS=======*/
TFT_eSPI tft=TFT_eSPI();
MPU6050 mpu;
HardwareSerial simSerial(2);
/*=======DEFINITIONS=======*/
#define MPU_INT_PIN 25
#define SIM_TX 16
#define SIM_RX 17
#define VIB 23
#define SIM_PWRKEY 12
#define MOSI 13
#define LED_PIN 14
#define CS 15
#define BUTTON_PIN 18
/*=======GLOBALVARIABLES=======*/
int16_t ax,ay,az,gx,gy,gz;
/*=======FALL DETECTION STATES=======*/
enum FallState{NORMAL,FREE_FALL,IMPACT_DETECTED,COUNTDOWN,SMS_SENT};
FallState fallState=NORMAL;
/*=======TIMERS=======*/
unsigned long stateStartTime=0;
unsigned long countdownStart=0;
bool smsSent=false;
/*=======THRESHOLDS (MPU6050 ±2g default)=======*/
const long FREE_FALL_THRESHOLD=8000;// <0.5g
const long IMPACT_THRESHOLD=40000;// >2.5g
const long ONE_G=16384;
const int IMMOBILE_TIME=1000;// 1 sec
const int COUNTDOWN_TIME=10000;// 10 sec
/*=======SIM7600 POWER=======*/
void powerOnSIM7600G(){
digitalWrite(SIM_PWRKEY,HIGH);
delay(1500);
digitalWrite(SIM_PWRKEY,LOW);
delay(5000);
}
/*=======SMS FUNCTION=======*/
void sendSMS(String number,String message){
Serial.println("Setting SMS text mode...");
simSerial.println("AT+CMGF=1");
delay(1000);
Serial.println("Sending number...");
simSerial.print("AT+CMGS=\"");
simSerial.print(number);
simSerial.println("\"");
delay(2000);// wait for > prompt
while(simSerial.available())Serial.write(simSerial.read());
Serial.println("Sending message content...");
simSerial.print(message);
delay(500);
simSerial.write(26);// CTRL+Z
delay(5000);
while(simSerial.available())Serial.write(simSerial.read());
Serial.println("SMS sent.");
}
/*=======SETUP=======*/
void setup(){
Serial.begin(115200);
simSerial.begin(9600,SERIAL_8N1,SIM_RX,SIM_TX);
pinMode(MOSI,OUTPUT);
pinMode(VIB, OUTPUT);
pinMode(LED_PIN,OUTPUT);
pinMode(CS,INPUT_PULLUP);
pinMode(SIM_PWRKEY,OUTPUT);
digitalWrite(MOSI,LOW);
digitalWrite(VIB,HIGH);
digitalWrite(LED_PIN,LOW);
digitalWrite(SIM_PWRKEY,LOW);
/* TFT INIT */
Serial.println("Initializing TFT...");
tft.init();
Serial.println("TFT Initialized");
tft.setRotation(1);
tft.fillScreen(TFT_BLACK);
tft.setTextColor(TFT_WHITE,TFT_BLACK);
tft.setTextSize(2);
tft.drawString("C.A.R.E Wristband",10,10);
/* MPU INIT */
Wire.begin();
mpu.initialize();
if(!mpu.testConnection()){
tft.drawString("MPU ERROR!",10,40);
while(1);
}
tft.drawString("MPU OK",10,40);
/* SIM INIT */
powerOnSIM7600G();
simSerial.println("AT");
delay(1000);
simSerial.println("AT+CMGF=1");
delay(1000);
tft.drawString("SIM OK",10,60);
}
/*=======MAIN LOOP=======*/
void loop(){
mpu.getMotion6(&ax,&ay,&az,&gx,&gy,&gz);
Serial.print("Accel X: ");Serial.print(ax);
Serial.print(" Y: ");Serial.print(ay);
Serial.print(" Z: ");Serial.print(az);
Serial.print("State: ");Serial.println(fallState);
long axl=ax;
long ayl=ay;
long azl=az;
long totalAccelSq=axl*axl+ayl*ayl+azl*azl;
long totalAccel=sqrt(totalAccelSq);
Serial.print("Total Accel: ");Serial.println(totalAccel);
unsigned long currentTime=millis();
switch(fallState){
/* ================= NORMAL ================= */
case NORMAL:
Serial.println("State: NORMAL");
smsSent=false;
digitalWrite(LED_PIN,LOW);
if(totalAccel<FREE_FALL_THRESHOLD){
fallState=FREE_FALL;
stateStartTime=currentTime;
}
break;
/* ================= FREE FALL ================= */
case FREE_FALL:
Serial.println("State: FREE_FALL");
if(totalAccel>IMPACT_THRESHOLD){
fallState=IMPACT_DETECTED;
stateStartTime=currentTime;
}
if(currentTime-stateStartTime>1000){
fallState=NORMAL;// timeout
}
break;
/* ================= IMPACT DETECTED ================= */
case IMPACT_DETECTED:
Serial.println("Impact Detected");
Serial.println("Starting Countdown...");
// Immediately start countdown after impact
fallState=COUNTDOWN;
countdownStart=currentTime;
digitalWrite(VIB,LOW);
digitalWrite(MOSI,HIGH);
digitalWrite(LED_PIN,HIGH);
break;
/* ================= COUNTDOWN ================= */
case COUNTDOWN:{
unsigned long timeLeft=COUNTDOWN_TIME-(currentTime-countdownStart);
int secondsLeft=timeLeft/1000;// convert milliseconds to seconds
Serial.print("Countdown: ");
Serial.println(timeLeft/1000);// show seconds left
// Cancel SMS only if the button is pressed
if(digitalRead(BUTTON_PIN)==LOW){
digitalWrite(VIB,HIGH);
digitalWrite(MOSI,LOW);
digitalWrite(LED_PIN,LOW);
fallState=NORMAL;// cancel alert
break;
}
// Send SMS automatically after 10 seconds
if(!smsSent&&currentTime-countdownStart>=COUNTDOWN_TIME){
sendSMS("+639453691069","Alert: Possible fall detected!");
smsSent=true;
digitalWrite(VIB, HIGH);
fallState=SMS_SENT;
}
// ==== TFT Countdown Display ====
tft.fillRect(0,90,240,40,TFT_BLACK);// clear previous text
tft.setCursor(10,90);
tft.setTextSize(3);// bigger text for countdown
tft.setTextColor(TFT_RED,TFT_BLACK);// red countdown
tft.printf("Countdown: %02d",secondsLeft);
break;
}
/* ================= SMS SENT ================= */
case SMS_SENT:
// Keep vibrating until standing up
// Standing = around 1g + movement detected
if(totalAccel>14000&&totalAccel<19000&&(abs(gx)>3000||abs(gy)>3000||abs(gz)>3000)){
digitalWrite(MOSI,LOW);
digitalWrite(VIB,HIGH);
digitalWrite(LED_PIN,LOW);
fallState=NORMAL;
}
break;
}
// ==== TFT STATUS UPDATE (lightweight) ====
if(fallState!=COUNTDOWN){// skip during countdown
tft.fillRect(0,90,240,40,TFT_BLACK);// clear area
tft.setCursor(10,90);
tft.setTextSize(2);// normal size
tft.setTextColor(TFT_WHITE,TFT_BLACK);// normal color
if(fallState==NORMAL){
tft.print("Status: Normal");
}else if(fallState==SMS_SENT){
tft.print("Status: SMS Sent");
}
}
delay(20);// ~50Hz sampling
}
