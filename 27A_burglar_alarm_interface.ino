#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include "switch.h"
#include "UpnpBroadcastResponder.h"
#include "CallbackFunction.h"

// prototypes
boolean connectWifi(); // router handed out 192.168.1.169 for this

//on/off callbacks 
bool Sec27ASetOn();
bool Sec27ASetOff();
bool Sec27AUnsetOn();
bool Sec27AUnsetOff();
bool Sec27APanicOn();
bool Sec27APanicOff();
bool AlarmSetLockout = LOW; // HIGH is lockout state

// Change this before you flash
const char* ssid = "SmartStuff";
const char* password ="Password123456";

boolean wifiConnected = false;

UpnpBroadcastResponder upnpBroadcastResponder;

Switch *Sec27ASet = NULL;
Switch *Sec27AUnset = NULL;
Switch *Sec27APanic = NULL;

bool isSec27ASetOn = false;
bool isSec27AUnsetOn = false;
bool isSec27APanicOn = false;
bool Sec27ASetState = false;
bool PrevSec27ASetState = false;
bool Sec27ASoundingState = false;
bool PrevSec27ASoundingState = false;

const int SetUnsetInputPin = 0;  // SetUnsetInputPin pin.
const int GPIO2 = 2;  // GPIO2 pin. used as LED Driver

const int AlarmSoundingInputPin = 3;  // 
/*

GPIO1 (TXD) = unused exept for serial debug
Onboard relay1 = AlarmSet/Unset control
Onboard relay2 = Panic Input or outside siren
*/

byte VBNumber = 33; // 33 is the watchdog post value for the 27A Security Interface

const char* WatchDogHost = "192.168.1.60"; // ip address of the watchdog esp8266

long WatchDogCounterLoopThreshold = 200;// value of 30 is about 5secs. 200 is about 20 sec
long WatchDogLoopCounter = 0;

byte rel1ON[] = {0xA0, 0x01, 0x01, 0xA2};  //Hex command to send to serial for open relay 1 - set/unset alarm
byte rel1OFF[] = {0xA0, 0x01, 0x00, 0xA1}; //Hex command to send to serial for close relay 1
byte rel2ON[] = {0xA0, 0x02, 0x01, 0xA3};  //Hex command to send to serial for open relay 2 - Panic zone or outside siren
byte rel2OFF[] = {0xA0, 0x02, 0x00, 0xA2}; //Hex command to send to serial for close relay 2

void setup()
{
 
  pinMode(GPIO2, OUTPUT); 
     
  Serial.begin(115200);
    delay(2000);

  Serial.println("Booting 27a Security Interface...");
  delay(2000);

  //flash fast a few times to indicate CPU is booting
  digitalWrite(GPIO2, LOW); 
  delay(100);    
  digitalWrite(GPIO2, HIGH);
  delay(100); 
  digitalWrite(GPIO2, LOW); 
  delay(100);    
  digitalWrite(GPIO2, HIGH);
  delay(100);   
  digitalWrite(GPIO2, LOW); 
  delay(100);   
  digitalWrite(GPIO2, HIGH);
  delay(100);   
   digitalWrite(GPIO2, LOW); 
  delay(100);    
  digitalWrite(GPIO2, HIGH);
  delay(100); 
  digitalWrite(GPIO2, LOW); 
  delay(100);    
  digitalWrite(GPIO2, HIGH);
  delay(100);   
  digitalWrite(GPIO2, LOW); 
  delay(100);   
  digitalWrite(GPIO2, HIGH);
  
  Serial.println("Delaying a bit...");
  delay(2000);   
  
  // Initialise wifi connection
  wifiConnected = connectWifi();
     
  if(wifiConnected){

  //flash slow a few times to indicate wifi connected OK
  digitalWrite(GPIO2, LOW); 
  delay(1000);    
  digitalWrite(GPIO2, HIGH);
  delay(1000); 
  digitalWrite(GPIO2, LOW); 
  delay(1000);    
  digitalWrite(GPIO2, HIGH);
   delay(1000); 
  digitalWrite(GPIO2, LOW); 
  delay(1000);    
  digitalWrite(GPIO2, HIGH);
    
    upnpBroadcastResponder.beginUdpMulticast();
    
    // Define your switches here. Max 10
    // Format: Alexa invocation name, local port no, on callback, off callback
    Sec27ASet = new Switch("Enable 27A Perimeter Security", 67, Sec27ASetOn, Sec27ASetOff);
    Sec27AUnset = new Switch("Disable 27A Perimeter Security", 68, Sec27AUnsetOn, Sec27AUnsetOff);
    Sec27APanic = new Switch("27A Panic", 69, Sec27APanicOn, Sec27APanicOff);

    Serial.println("Adding switches upnp broadcast responder");
    upnpBroadcastResponder.addDevice(*Sec27ASet);
    upnpBroadcastResponder.addDevice(*Sec27AUnset);
    upnpBroadcastResponder.addDevice(*Sec27APanic);
    
  }
       digitalWrite(GPIO2, HIGH); // turn off LED 

   Serial.println("Making AlarmSoundingInputPin into an INPUT"); 
   pinMode(AlarmSoundingInputPin, FUNCTION_3);
   pinMode(AlarmSoundingInputPin, INPUT);

    Serial.println("Making SetUnsetInputPin into an INPUT"); // used to detect 27A Security  Set/Unset state
  
   pinMode(SetUnsetInputPin, FUNCTION_3);
   pinMode(SetUnsetInputPin, INPUT);
   
}
     
void loop()
{
  
  Sec27ASetState = digitalRead(SetUnsetInputPin); //
  delay(100);  
  if (Sec27ASetState == LOW) {
      if (PrevSec27ASetState == HIGH){ 
        Serial.println("27a security has just entered Set State");
        VBNumber = 2; // 27a Security Set code
        ProxyPost();
      }
  }       
  
      
  if (Sec27ASetState == HIGH) {
       if (PrevSec27ASetState == LOW){ 
        Serial.println("27a security has just enterted Unset State ");
        VBNumber = 3; // 27a Security Unset code
        ProxyPost();
      }
  }      


  Sec27ASoundingState = !digitalRead(AlarmSoundingInputPin); // Used for Detecting the Alarm sounding
  delay(100);  
  if (Sec27ASoundingState == LOW) {
      if (PrevSec27ASoundingState == HIGH){ 
        Serial.println("27a security  has just gone into the alarm sounding state ");
        VBNumber = 4; // 27a Security Sounding code
        ProxyPost();
      }
  }       
  
      
  if (Sec27ASoundingState == HIGH) {
       if (PrevSec27ASoundingState == LOW){ 
        Serial.println("27a security has just stopped sounding ");
       } 
   }      

    PrevSec27ASoundingState = Sec27ASoundingState;   // remember prev state for next pass
    PrevSec27ASetState = Sec27ASetState; // edge detection of Burglar Alarm state
    
  if(wifiConnected){
     // digitalWrite(GPIO2, LOW); // turn on LED with voltage Low
      upnpBroadcastResponder.serverLoop();

      Sec27APanic->serverLoop();
      Sec27AUnset->serverLoop();
      Sec27ASet->serverLoop();
   }

   WatchDogLoopCounter = WatchDogLoopCounter +1;
   //Serial.println(WatchDogLoopCounter);
   if (WatchDogLoopCounter > WatchDogCounterLoopThreshold) {
        //PanelBuzzerCount = (PanelBuzzerCountThreshold - 4);
        WatchDogLoopCounter = 0;
        VBNumber = 33; // 27A security watchdog code
        WatchDogPost();
   }
 
} // end Void Loop

bool Sec27ASetOn() {
    Serial.println("Request to Set Burglar Alarm received SW #1 On...");

      if (Sec27ASetState == HIGH) { // only pulse relay if Burglar Alarm is currently Unset
          //sometimes alexa sends this request again about 2 secs later which turned the alarm off again on the second request
          // we need to lockout multiple turn on requests that are received in quick succession
          // or maybe, just extend the pulse duration ? (was 1 sec) - didnt work...
          if (AlarmSetLockout == LOW) { // only allows set routine to run once, initally needed the alarm off request to release this
            // but this gave rise to problems if the alarm was set via alexa and reset via keypads or RF remote.
            // changed to reset automatically after 5 secs
             
            Serial.println("Burglar Alarm is Unset - pulsing relay to Set it");
            AlarmSetLockout = HIGH; // set the lockout
            
            Serial.println("XXX Pulsing Relay on ...");
  
                // Turn on #1 Relay
                delay(10);
                Serial.write(rel1ON, sizeof(rel1ON));
                delay(10);
                Serial.println("Turning Relay#1 On ...");
                                  
                // Turn on #1 Relay
                delay(10);
                Serial.write(rel1ON, sizeof(rel1ON));
                delay(10);
                Serial.println("Turning Relay#1 On ...");
               
                delay(1000);    ;
                
            Serial.println("XXX Pulsing Relay off again ..."); // this makes a pulse which is what the security system wants
  
                // Turn off #1 Relay
                delay(10);
                Serial.write(rel1OFF, sizeof(rel1OFF));
                delay(10);
                Serial.println("Turning Relay#1 Off ...");
                                  
                // Turn off #1 Relay
                delay(10);
                Serial.write(rel1OFF, sizeof(rel1OFF));
                delay(10);
                Serial.println("Turning Relay#1 Off ...");

            }
          }
      else {
          Serial.println("27A Security is already Set - not pulsing relay!");
      }
         
    isSec27ASetOn = false;    
    return Sec27ASetState;
}

bool Sec27ASetOff() { 
    
    Serial.println("Request to Set 27A Security received SW#1 Off ...");
    Serial.println("This should never happen");
                  
    isSec27ASetOn = false;
    return Sec27ASetState;
}

bool Sec27AUnsetOn() {
    Serial.println("Request to Unset 27A Security received SW#2 On");

      if (Sec27ASetState == LOW) { // only pulse relay if Burglar Alarm is currently Set
                    Serial.println("XXX Pulsing Relay on ...");
              // AlarmSetLockout = LOW; // reset the lockout for the turn on function
              // this in asymetric and doesnt have a lockout for preventing multiple offs like the on function
              // becasue the alarm unsets immediatly and prevents any subsequent requests from alexa as being
              // processed as on commands.
              // I think.
              
              // Turn on #1 Relay
              delay(10);
              Serial.write(rel1ON, sizeof(rel1ON));
              delay(10);
              Serial.println("Turning Relay#1 On ...");
                                
              // Turn on #1 Relay
              delay(10);
              Serial.write(rel1ON, sizeof(rel1ON));
              delay(10);
              Serial.println("Turning Relay#1 On ...");
             
              delay(1000);    ;
              
          Serial.println("XXX Pulsing Relay off again ..."); // this makes a pulse which is what the security system wants

              // Turn off #1 Relay
              delay(10);
              Serial.write(rel1OFF, sizeof(rel1OFF));
              delay(10);
              Serial.println("Turning Relay#1 Off ...");
                                
              // Turn off #1 Relay
              delay(10);
              Serial.write(rel1OFF, sizeof(rel1OFF));
              delay(10);
              Serial.println("Turning Relay#1 Off ...");
      }
      else {
          Serial.println("27A Security is already Unset, not pulsing relay...");
      }
      
    isSec27AUnsetOn = false;
    return Sec27ASetState;
}

bool Sec27AUnsetOff() {  

    Serial.println("Request to Unset 27A Security received (SW#2 Off)");
    Serial.println("This should never happen");
  
  isSec27AUnsetOn = false;
  return Sec27ASetState;
}

bool Sec27APanicOn() {
    Serial.println("Request to set Panic Mode received SW#3 On");
    // Turn on #2 Relay
              delay(10);
              Serial.write(rel2ON, sizeof(rel2ON));
              delay(10);
              Serial.println("Turning Relay#2 On ...");

      // Turn on #2 Relay
              delay(10);
              Serial.write(rel2ON, sizeof(rel2ON));
              delay(10);
              Serial.println("Turning Relay#2 On ...");
            

      
    isSec27APanicOn = false;
    return isSec27APanicOn;
}

bool Sec27APanicOff() {  

    Serial.println("Request to stop Panic Mode received (SW#3 Off)");
    //Serial.println("This should never happen");

        // Turn off #2 Relay
              delay(10);
              Serial.write(rel2OFF, sizeof(rel2OFF));
              delay(10);
              Serial.println("Turning Relay#2 Off ...");

       // Turn off #2 Relay
              delay(10);
              Serial.write(rel2OFF, sizeof(rel2OFF));
              delay(10);
              Serial.println("Turning Relay#2 Off ...");

  
  isSec27APanicOn = false;
  return Sec27ASetState;
}

// connect to wifi – returns true if successful or false if not
boolean connectWifi(){
  boolean state = true;
  int i = 0;
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("Connecting to WiFi Network");

  // Wait for connection
  Serial.print("Connecting ...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(5000);
    Serial.print(".");
    if (i > 10){
      state = false;
      break;
    }
    i++;
  }
  
  if (state){
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println("");
    Serial.println("Connection failed. Bugger");
  }
  
  return state;
}

void ProxyPost() {
TwitchLED();
// assumes VBNumber set to desired VB call to be made

// 2 is Burglar Alarm has Seted
// 3 is Burglar Alarm is Unset
// 4 is Burglar Alarm Sounding
  Serial.print("Requesting POST to WatchDog ");
  Serial.println(VBNumber);

  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(WatchDogHost, httpPort)) {
    Serial.println("connection failed");
    return;
  }

String data = "";

   // Send request to the server:
   client.println("POST / HTTP/1.1");
   //Serial.println("VB button"+(String(VBNumber))+" request sent");
   client.println("Host: ProxyRequest" +(String(VBNumber))); // this endpoint value gets to the server and is used to transfer the identity of the calling slave
   client.println("Accept: */*"); // this gets to the server!
   client.println("Content-Type: application/x-www-form-urlencoded");
   client.print("Content-Length: ");
   client.println(data.length());
   client.println();
   client.print(data);
  
   delay(500); // Can be changed
  if (client.connected()) { 
    client.stop();  // DISCONNECT FROM THE SERVER
  }
  Serial.println();
  Serial.println("closing connection");
  delay(1000);
}// end ProxyPost

void WatchDogPost() {

TwitchLED();

// assumes VBNumber set to desired VB call to be made

// 33 is Burglar Alarm watchdog

  Serial.print("Requesting POST to WatchDog ");
  Serial.println(VBNumber);

  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(WatchDogHost, httpPort)) {
    Serial.println("connection failed");
    return;
  }

String data = "";

   // Send request to the server:
   client.println("POST / HTTP/1.1");
   //Serial.println("VB button"+(String(VBNumber))+" request sent");
   client.println("Host: WatchDog Endpoint" +(String(VBNumber))); // this endpoint value gets to the server and is used to transfer the identity of the calling slave
   client.println("Accept: */*"); // this gets to the server!
   client.println("Content-Type: application/x-www-form-urlencoded");
   client.print("Content-Length: ");
   client.println(data.length());
   client.println();
   client.print(data);
  
   delay(500); // Can be changed
  if (client.connected()) { 
    client.stop();  // DISCONNECT FROM THE SERVER
  }
  Serial.println();
  Serial.println("closing connection");
  delay(1000);
}// end WatchDogPost

void TwitchLED () {

  pinMode(GPIO2, OUTPUT); // switch to an output
  
  digitalWrite(GPIO2, LOW);
  delay(10);
  digitalWrite(GPIO2, HIGH);

  pinMode(GPIO2, INPUT); // switch back to an input
}
