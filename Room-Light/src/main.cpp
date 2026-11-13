#include <Arduino.h>
#define relay 5
#define sensor 6

unsigned long previousmillis = 0;
unsigned long sensor_on_time = 10000;
unsigned long serial = 0;

bool sensor_state = false;

void setup() 
{
  pinMode(relay, OUTPUT);
  pinMode(sensor, INPUT);
  digitalWrite(relay, HIGH);
  Serial.begin(115200);
  
}

void loop() 
{
  unsigned long currentmillis = millis();
  if(digitalRead(sensor) == HIGH)
  {
    digitalWrite(relay, LOW);
    sensor_state = true;
    previousmillis = currentmillis;
  }
  if(sensor_state && (currentmillis - previousmillis >= sensor_on_time))
  {
    digitalWrite(relay, HIGH);
    sensor_state = false;
  }
  if(currentmillis - serial >= 2500)
  {
      serial = currentmillis;
      Serial.print("Sensor state:");
      Serial.println(digitalRead(sensor));
      Serial.println(" ");
      Serial.print("Relay state:");
      Serial.println(digitalRead(relay));
  }


}
   
