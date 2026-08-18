#include "Arduino_RouterBridge.h"
#include <Wire.h>

const int PWMA=5, AIN1=4, AIN2=7, PWMB=6, BIN1=8, BIN2=12, STBY=13;
const int ENC_LEFT=2, ENC_RIGHT=3;
const int CLIFF_LEFT=9, CLIFF_RIGHT=10, REAR_IR=11;
const int TRIG_PIN=A0, ECHO_PIN=A1;
const uint8_t INA226_ADDR=0x40, MPU6050_ADDR=0x68;
const int DRIVE_SPEED=180;
const int DOCK_SPEED=110;
const float STOP_DISTANCE_CM=15.0;
const unsigned long TELEMETRY_INTERVAL_MS=200;

struct SensorRegistry {
  float ultrasonic_cm=-1; bool ultrasonic_valid=false;
  long enc_left_ticks=0, enc_right_ticks=0; bool encoders_valid=false;
  bool cliff_left=false, cliff_right=false, rear_ir=false; bool ir_valid=false;
  float bus_voltage_v=-1; bool ina226_valid=false;
  int16_t accel_x=0, accel_y=0, accel_z=0; bool mpu6050_valid=false;
  char current_command='S';
  bool obstacle_override=false;
  bool dock_mode=false; bool estop=false;
} sensors;

volatile long leftTicksRaw=0, rightTicksRaw=0;
bool mpuInit=false;
unsigned long lastTelemetryMs=0;
bool nudgeActive=false; char nudgeDir='S'; unsigned long nudgeEndMs=0;

void leftTickISR(){ leftTicksRaw++; }
void rightTickISR(){ rightTicksRaw++; }

void setSpeed(int s){}
void leftFwd(int s){ digitalWrite(AIN1,HIGH); digitalWrite(AIN2,LOW); analogWrite(PWMA,s); }
void leftBack(int s){ digitalWrite(AIN1,LOW); digitalWrite(AIN2,HIGH); analogWrite(PWMA,s); }
void leftStop(){ analogWrite(PWMA,0); }
void rightFwd(int s){ digitalWrite(BIN1,HIGH); digitalWrite(BIN2,LOW); analogWrite(PWMB,s); }
void rightBack(int s){ digitalWrite(BIN1,LOW); digitalWrite(BIN2,HIGH); analogWrite(PWMB,s); }
void rightStop(){ analogWrite(PWMB,0); }

void moveDir(char cmd, int spd){
  switch(cmd){
    case 'F': case 'f': leftFwd(spd); rightFwd(spd); break;
    case 'B': case 'b': leftBack(spd); rightBack(spd); break;
    case 'L': case 'l': leftBack(spd); rightFwd(spd); break;
    case 'R': case 'r': leftFwd(spd); rightBack(spd); break;
    default: leftStop(); rightStop(); break;
  }
}
void motorsStop(){ leftStop(); rightStop(); }

void read_ultrasonic(){
  digitalWrite(TRIG_PIN,LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN,LOW);
  long dur=pulseIn(ECHO_PIN,HIGH,30000);
  if(dur==0){ sensors.ultrasonic_valid=false; sensors.ultrasonic_cm=-1; }
  else { sensors.ultrasonic_cm=(dur*0.0343)/2.0; sensors.ultrasonic_valid=true; }
}

void read_encoders(){
  noInterrupts();
  sensors.enc_left_ticks=leftTicksRaw; sensors.enc_right_ticks=rightTicksRaw;
  interrupts();
  sensors.encoders_valid=true;
}

void read_cliff_and_rear(){
  sensors.cliff_left =(digitalRead(CLIFF_LEFT)==LOW);
  sensors.cliff_right=(digitalRead(CLIFF_RIGHT)==LOW);
  sensors.rear_ir =(digitalRead(REAR_IR)==LOW);
  sensors.ir_valid=true;
}

void read_ina226(){
  Wire.beginTransmission(INA226_ADDR); Wire.write(0x02);
  if(Wire.endTransmission(false)!=0){ sensors.ina226_valid=false; sensors.bus_voltage_v=-1; return; }
  Wire.requestFrom((int)INA226_ADDR,2);
  if(Wire.available()<2){ sensors.ina226_valid=false; return; }
  int16_t raw=(Wire.read()<<8)|Wire.read();
  sensors.bus_voltage_v=raw*0.00125;
  sensors.ina226_valid=true;
}

void init_mpu(){
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x6B); Wire.write(0);
  mpuInit=(Wire.endTransmission(true)==0);
}

void read_mpu(){
  if(!mpuInit) init_mpu();
  if(!mpuInit){ sensors.mpu6050_valid=false; return; }
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x3B);
  if(Wire.endTransmission(false)!=0){ sensors.mpu6050_valid=false; mpuInit=false; return; }
  Wire.requestFrom((int)MPU6050_ADDR,6);
  if(Wire.available()<6){ sensors.mpu6050_valid=false; return; }
  sensors.accel_x=(Wire.read()<<8)|Wire.read();
  sensors.accel_y=(Wire.read()<<8)|Wire.read();
  sensors.accel_z=(Wire.read()<<8)|Wire.read();
  sensors.mpu6050_valid=true;
}

void handleDrive(String cmd){
  if(cmd.length()==0) return;
  sensors.current_command=cmd.charAt(0);
  nudgeActive=false;
}

void handleNudge(String cmd, int ms){
  if(cmd.length()==0 || ms<=0) return;
  nudgeDir=cmd.charAt(0);
  nudgeActive=true;
  nudgeEndMs=millis()+ms;
}

void handleDockMode(int on){ sensors.dock_mode=(on!=0); }

void handleEstop(int on){
  sensors.estop=(on!=0);
  if(sensors.estop){ sensors.current_command='S'; nudgeActive=false; motorsStop(); }
}

void setup(){
  int outs[]={PWMA,AIN1,AIN2,PWMB,BIN1,BIN2,STBY};
  for(int i=0;i<7;i++) pinMode(outs[i],OUTPUT);
  digitalWrite(STBY,HIGH);

  pinMode(ENC_LEFT,INPUT_PULLUP); pinMode(ENC_RIGHT,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_LEFT), leftTickISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENC_RIGHT),rightTickISR,FALLING);

  pinMode(CLIFF_LEFT,INPUT); pinMode(CLIFF_RIGHT,INPUT);
  pinMode(REAR_IR,INPUT);
  pinMode(TRIG_PIN,OUTPUT); pinMode(ECHO_PIN,INPUT);

  Wire.begin(); init_mpu();
  motorsStop();

  Bridge.begin();
  Monitor.begin();
  Bridge.provide_safe("drive", handleDrive);
  Bridge.provide_safe("nudge", handleNudge);
  Bridge.provide_safe("dockmode", handleDockMode);
  Bridge.provide_safe("estop", handleEstop);
}

void loop(){
  read_ultrasonic();
  read_encoders();
  read_cliff_and_rear();
  read_ina226();
  read_mpu();
  
  sensors.obstacle_override =
      !sensors.dock_mode &&
      sensors.ultrasonic_valid &&
      (sensors.ultrasonic_cm < STOP_DISTANCE_CM) &&
      (sensors.current_command=='F' || sensors.current_command=='f');

  if(sensors.estop){
    motorsStop();
  }
  else if(nudgeActive){
    if(millis() >= nudgeEndMs){ nudgeActive=false; motorsStop(); }
    else { moveDir(nudgeDir, sensors.dock_mode ? DOCK_SPEED : DRIVE_SPEED); }
  }
  else if(sensors.obstacle_override){
    motorsStop();
  }
  else {
    moveDir(sensors.current_command, sensors.dock_mode ? DOCK_SPEED : DRIVE_SPEED);
  }

  unsigned long now=millis();
  if(now-lastTelemetryMs>=TELEMETRY_INTERVAL_MS){
    lastTelemetryMs=now;
    Bridge.notify("telemetry",
      sensors.ultrasonic_cm, sensors.ultrasonic_valid,
      (long)sensors.enc_left_ticks,(long)sensors.enc_right_ticks,sensors.encoders_valid,
      sensors.cliff_left,sensors.cliff_right,sensors.rear_ir,sensors.ir_valid,
      sensors.bus_voltage_v,sensors.ina226_valid,
      (int)sensors.accel_x,(int)sensors.accel_y,(int)sensors.accel_z,sensors.mpu6050_valid,
      String(sensors.current_command),sensors.obstacle_override,
      sensors.dock_mode,sensors.estop
    );
  }
}
