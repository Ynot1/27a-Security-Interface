#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include "switch.h"
#include "UpnpBroadcastResponder.h"
#include "CallbackFunction.h"

// prototypes
boolean connectWifi();  // router handed out 192.168.1.169 for this

//on/off callbacks
bool Sec27ASetOn();
bool Sec27ASetOff();
bool Sec27AUnsetOn();
bool Sec27AUnsetOff();
bool Sec27APanicOn();
bool Sec27APanicOff();
bool AlarmSetLockout = LOW;  // HIGH is lockout state

// Change this before you flash
const char* ssid = "SmartStuff";
const char* password = "Password123456";

boolean wifiConnected = false;

UpnpBroadcastResponder upnpBroadcastResponder;

Switch* Sec27ASet = NULL;
Switch* Sec27AUnset = NULL;
Switch* Sec27APanic = NULL;

bool isSec27ASetOn = false;
bool isSec27AUnsetOn = false;
bool isSec27APanicOn = false;
bool Sec27ASetState = false;
bool PrevSec27ASetState = false;
bool Sec27ASoundingState = false;
bool PrevSec27ASoundingState = false;

const int SetUnsetInputPin = 0;  // SetUnsetInputPin pin.
const int GPIO2 = 2;             // GPIO2 pin. used as LED Driver

const int AlarmSoundingInputPin = 3;  //
/*

GPIO0 - Alarm Panel Set/Unset input
GPIO1 (TXD) = unused exept for serial debug
GPIO2 -LED Driver 
GPIO3 (RXD) - potentially the AlarmSounding input . Not used by current code except logging alarm activity towards the local web page and watchdog proxt

Onboard relay1 = AlarmSet/Unset control
Onboard relay2 = Panic Input or outside siren
*/

byte VBNumber =40;  // 40 is the watchdog post value for the 27A Security Interface
String VBNumberString =""; // also need a string as leading zero needed for later string matching accuracy

const char* WatchDogHost = "192.168.1.60";  // ip address of the watchdog esp8266

long WatchDogCounterLoopThreshold = 200;  // value of 30 is about 5secs. 200 is about 20 sec
long WatchDogLoopCounter = 0;

byte rel1ON[] = { 0xA0, 0x01, 0x01, 0xA2 };   //Hex command to send to serial for open relay 1 - set/unset alarm
byte rel1OFF[] = { 0xA0, 0x01, 0x00, 0xA1 };  //Hex command to send to serial for close relay 1
byte rel2ON[] = { 0xA0, 0x02, 0x01, 0xA3 };   //Hex command to send to serial for open relay 2 - Panic zone or outside siren
byte rel2OFF[] = { 0xA0, 0x02, 0x00, 0xA2 };  //Hex command to send to serial for close relay 2

WiFiServer server(80);  // start bWebServer
String ledState = "OFF";
const int ledPin = 2;  // Built-in LED pin

byte ProxyLogArrayIndex = 0;

byte currentseconds = 0;
byte currentminutes = 55;
byte currenthours = 12;
long currentday = 0;
long currentmonth = 0;
String ProxyLogArray[64];  // 20 events each containing Date [0], Time[1], Proxt [2]. last 3 entries (60,61,62) are headers
String LastRebootDate;
String LastRebootTime;
float UpTimeDays = 0;
byte ProxyRequestID = 0;
String ProxyRequestText;
bool SetTimeWasSuccesfull;
int DSTOffset = 1;

int StackedIndex = 0;
byte WebPageMode = 2;  // 1 = Setup, 2 = Runtime

unsigned long currentTime = millis();

unsigned long previousTime = 0;
unsigned long previousMillis = 0;


int AlarmSetLockoutCounter;
int AlarmSetLockoutCounterThreshold = 30;

// Set your Static IP address
IPAddress local_IP(192, 168, 1, 169);  // fixed IP address for the 27A Security Interface (this code)
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    // Google DNS (crucial for NTP time sync)
IPAddress secondaryDNS(8, 8, 4, 4);  // Optional backup DNS

void setup() {

  pinMode(GPIO2, OUTPUT);

  Serial.begin(115200);
  delay(1000);

  Serial.println("Booting 27a Security Interface...");
  delay(1000);

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

  Serial.println("Booting 27A Security Interface.    Delaying a bit...");
  delay(2000);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // Initialise wifi connection
  wifiConnected = connectWifi();

  if (wifiConnected) {

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
    Sec27ASet = new Switch("Enable 27A Perimeter Monitor", 67, Sec27ASetOn, Sec27ASetOff);
    Sec27AUnset = new Switch("Disable 27A Perimeter Monitor", 68, Sec27AUnsetOn, Sec27AUnsetOff);
    Sec27APanic = new Switch("27A Panic", 69, Sec27APanicOn, Sec27APanicOff);

    Serial.println("Adding switches upnp broadcast responder");
    upnpBroadcastResponder.addDevice(*Sec27ASet);
    upnpBroadcastResponder.addDevice(*Sec27AUnset);
    upnpBroadcastResponder.addDevice(*Sec27APanic);
  }
  digitalWrite(GPIO2, HIGH);  // turn off LED

  Serial.println("Making AlarmSoundingInputPin into an INPUT");
  pinMode(AlarmSoundingInputPin, FUNCTION_3);
  pinMode(AlarmSoundingInputPin, INPUT);

  Serial.println("Making SetUnsetInputPin into an INPUT");  // used to detect 27A Security  Set/Unset state

  pinMode(SetUnsetInputPin, FUNCTION_3);
  pinMode(SetUnsetInputPin, INPUT);

  // Start the server and print local IP address
  server.begin();
  Serial.println("");
  Serial.println("Wi-Fi connected.");
  Serial.print("IP address to visit: http://");
  Serial.println(WiFi.localIP());

  // populate event log headers
  ProxyLogArray[60] = "Date";
  ProxyLogArray[61] = "Time";
  ProxyLogArray[62] = "Event Info";

  ProxyLogArray[57] = "1st Entry date";
  ProxyLogArray[58] = "1st Entry time";
  ProxyLogArray[59] = "1st Entry Event";

  ProxyLogArray[54] = "2 Entry date";
  ProxyLogArray[55] = "2 Entry time";
  ProxyLogArray[56] = "2 Entry Event";

  ProxyLogArray[51] = "3 Entry date";
  ProxyLogArray[52] = "3 Entry time";
  ProxyLogArray[53] = "3 Entry Event";

  ProxyLogArray[30] = "1/2way date";
  ProxyLogArray[31] = " 1/2way Time";
  ProxyLogArray[32] = "1/2way Event Info";



  ProxyLogArray[3] = "2nd2Last Entry date";
  ProxyLogArray[4] = "2nd2Last Entry time";
  ProxyLogArray[5] = "2nd2Last Entry Event";

  ProxyLogArray[0] = "oldest Entry date";
  ProxyLogArray[1] = "oldest Entry time";
  ProxyLogArray[2] = "oldest Entry Event";

  SetTime();  // sync the clock..
  Serial.println(" Delaying 1 sec before trying clock sync again...");
  delay(1000);
  SetTime();  // sync the clock..
  Serial.println(" completed 2nd clock sync ..");
  // Load root certificate in DER format into WiFiClientSecure object
  bool res = 0;  //client.setCACert_P(caCert, caCertLen);
                 //if (!res) {
  Serial.println("Failed to load root CA certificate!");
  // while (true) {
  //  yield();
  // }
  //  Serial.println("root CA certificate loaded");
  //}

  // Populate " - " in all Proxy log slots

  Serial.println("populating array with - ");

  for (ProxyLogArrayIndex = 0; ProxyLogArrayIndex < 57; ProxyLogArrayIndex = ProxyLogArrayIndex + 1) {

    ProxyLogArray[ProxyLogArrayIndex] = " - ";
  }

  // Save reboot date/time
  LastRebootDate = (String(currentday) + " / " + String(currentmonth));
  LastRebootTime = (String(currenthours) + ":" + String(currentminutes) + ":" + String(currentseconds));

  // Insert reboot time as first event in event table
  Serial.println("populating array with boot time");
  ProxyLogArray[57] = (String(currentday) + " / " + String(currentmonth));
  ProxyLogArray[58] = (String(currenthours) + ":" + String(currentminutes) + ":" + String(currentseconds));
  ProxyLogArray[59] = "Restarted";

  // Rotate WDFailLog towards index 0 each log entry is 3 entries

  for (ProxyLogArrayIndex = 0; ProxyLogArrayIndex < 30; ProxyLogArrayIndex = ProxyLogArrayIndex + 1) {

    ProxyLogArray[ProxyLogArrayIndex] = ProxyLogArray[(ProxyLogArrayIndex + 3)];
  }




  Serial.println("end of void setup... Delaying 1 sec...");
  delay(1000);

}  // end of setup


void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    connectWifi();
    return;
  }

  Sec27ASetState = digitalRead(SetUnsetInputPin);  //
  delay(100);
  if (Sec27ASetState == LOW) {
    if (PrevSec27ASetState == HIGH) {
      Serial.println("27a security has just entered Set State");
      VBNumber = 02;  // 27a Security Set code
      VBNumberString = "02";
      ProxyPost();
      ProxyRequestText = "Alarm is Set";
      RotateProxyLogArray();
    }
  }


  if (Sec27ASetState == HIGH) {
    if (PrevSec27ASetState == LOW) {
      Serial.println("27a security has just enterted Unset State ");
      VBNumber = 03;  // 27a Security Unset code
      VBNumberString = "03";
      ProxyPost();
      ProxyRequestText = "Alarm is UnSet";
      RotateProxyLogArray();
    }
  }


  Sec27ASoundingState = !digitalRead(AlarmSoundingInputPin);  // Used for Detecting the Alarm sounding
  delay(100);
  if (Sec27ASoundingState == LOW) {
    if (PrevSec27ASoundingState == HIGH) {
      Serial.println("27a security  has just gone into the alarm sounding state ");
      VBNumber = 04;  // 27a Security Sounding code
      VBNumberString = "04";
      ProxyPost();
      ProxyRequestText = "Alarm is Sounding";
      RotateProxyLogArray();
    }
  }


  if (Sec27ASoundingState == HIGH) {
    if (PrevSec27ASoundingState == LOW) {
      Serial.println("27a security has just stopped sounding ");
      ProxyRequestText = "Alarm has stopped Sounding";
      RotateProxyLogArray();
    }
  }

  PrevSec27ASoundingState = Sec27ASoundingState;  // remember prev state for next pass
  PrevSec27ASetState = Sec27ASetState;            // edge detection of Burglar Alarm state

  if (wifiConnected) {
    // digitalWrite(GPIO2, LOW); // turn on LED with voltage Low
    upnpBroadcastResponder.serverLoop();

    Sec27APanic->serverLoop();
    Sec27AUnset->serverLoop();
    Sec27ASet->serverLoop();
  }

  WatchDogLoopCounter = WatchDogLoopCounter + 1;
  //Serial.println(WatchDogLoopCounter);
  if (WatchDogLoopCounter > WatchDogCounterLoopThreshold) {
    //PanelBuzzerCount = (PanelBuzzerCountThreshold - 4);
    WatchDogLoopCounter = 0;
    VBNumber = 40;  // 27A security watchdog code
    WatchDogPost();
  }

  // Listen for incoming web browser clients
  WiFiClient client = server.available();

  if (client) {
    Serial.println("New Client Connected.");
    String currentLine = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);

        // If you get a newline character, process the HTTP request
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Send standard HTTP response headers
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");
            client.println();

            // --- START OF HTML WEB PAGE ---
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");

            // Simple CSS styling for mobile-responsiveness
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #555555;}</style></head>");

            // Web Page Heading
            client.println("<body><h1>27A Security Interface Log</h1>");

            // Display current state, and show ON/OFF buttons
            client.println("<p>LED Status: <strong>" + ledState + "</strong></p>");
            if (ledState == "OFF") {
              client.println("<p><a href=\"/H\"><button class=\"button\">TURN ON</button></a></p>");

            } else {
              client.println("<p><a href=\"/L\"><button class=\"button button2\">TURN OFF</button></a></p>");
            }

            //Display Alarm set/Unset State

            if (Sec27ASetState == HIGH) {  // High is alarm unset state
              client.println("<p>Alarm Panel Status: <strong> UNSET (disabled/off) </strong></p>");
              client.println("<p><a href=\"/SET\"><button class=\"button\">SET Alarm</button></a></p>");

            } else {
              client.println("<p>Alarm Panel Status: <strong> SET (enabled/on) </strong></p>");
              client.println("<p><a href=\"UNSET\"><button class=\"button button2\">UnSET Alarm</button></a></p>");
            }
            // copy in watchdog proxylog code


            // display proxy log
            // Display date
            client.println("<p>Current Date is " + String(currentday) + " / " + String(currentmonth) + "</p>");


            // Display current time of day
            client.println("<p>Current Time is " + String(currenthours) + ":" + String(currentminutes) + ":" + String(currentseconds) + ":" + "</p>");

            // Display last Reboot Time
            client.println("<p>Last Restart was " + LastRebootTime + " on " + LastRebootDate + " which was " + String(UpTimeDays) + " days ago" + "</p>");


            // Display log 60 - 0


            // display proxylog2
            for (ProxyLogArrayIndex = 63; ProxyLogArrayIndex > 0; ProxyLogArrayIndex = ProxyLogArrayIndex - 3) {

              client.println("<p>" + (ProxyLogArray[ProxyLogArrayIndex]) + "       " + (ProxyLogArray[(ProxyLogArrayIndex + 1)]) + " " + (ProxyLogArray[(ProxyLogArrayIndex + 2)]) + "</p>");
            }

            //Display Clock Resync, Spare1 Buttons, Spare2 Button
            client.println("<p><a href=\"/RTCReSync\"><button class=\"buttonsmall\">RTC ReSync</button></a> <a href=\"/Reboot\"><button class=\"buttonsmall\">Reboot</button></a> <a href=\"/ProxyLog2\"> <button class=\"buttonsmall\">Spare2</button></a></p>");

            // copy in watchdog proxylog code

            client.println("</body></html>");


            // --- END OF HTML WEB PAGE ---

            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }

        // Process button actions embedded in URL paths
        if (currentLine.endsWith("GET /H")) {
          digitalWrite(ledPin, LOW);
          Serial.println("Turning LED On ...");
          ledState = "ON";
        }
        if (currentLine.endsWith("GET /L")) {
          digitalWrite(ledPin, HIGH);
          Serial.println("Turning LED Off ...");
          ledState = "OFF";
        }
        if (currentLine.endsWith("GET /SET")) {
          Sec27ASetOn();
        }
        if (currentLine.endsWith("GET /UNSET")) {
          Sec27AUnsetOn();
        }
        if (currentLine.endsWith("GET /RTCReSync")) {
          SetTime();
        }
        if (currentLine.endsWith("GET /Reboot")) {
          ESP.restart();
        }
      }
    }
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
  }

  //Keep time
  if (millis() >= (previousMillis)) {
    //Serial.print(" millis = ");

    previousMillis = previousMillis + 1000;
    //Serial.print(" prevmillis = ");
    //Serial.print(String (previousMillis));
    // should be here every second...

    // Maintain RTC

    currentseconds = currentseconds + 1;
    if (currentseconds == 60) {
      // Things to do every second here
      currentseconds = 0;
      currentminutes = currentminutes + 1;
      //Serial.println("another minute has passed");
    }
    if (currentminutes == 60) {


      // Things to do every hour here
      currentminutes = 0;
      currenthours = currenthours + 1;
      UpTimeDays = UpTimeDays + 0.0417;  // (1/24)
                                         //Serial.println("UpTimeDays =  " + String(UpTimeDays) );
    }
    if (currenthours == 24) {
      // Things to do every day here
      currentseconds = 0;
      currentminutes = 0;
      currenthours = 0;
      ProxyRequestText = "Midnight Rollover";
      RotateProxyLogArray();
      //SetTime();  // resync the clock
 

      //UpTimeDays = UpTimeDays + 0.5; IDK why but it sometimes counts 2X at this point
    }

    //Detect 09:45 and resync the RTC
if (currenthours == 9) {
    if (currentminutes == 45) {
        if (currentseconds == 0){

            ProxyRequestText = "Its 09:45, as good time as any to resync";
             RotateProxyLogArray();
            SetTime(); // resync the clock
            currentseconds = 1; //make sure this only runs once
        }
    }

}

}  // end 1 second


  // Check if the AlarmSetLockout needs to be released

  if (AlarmSetLockout == HIGH) {  // lockout is active
    AlarmSetLockoutCounter = AlarmSetLockoutCounter + 1;
    Serial.println(AlarmSetLockoutCounter);
    if (AlarmSetLockoutCounter > AlarmSetLockoutCounterThreshold) {  // its time to release lockout
      AlarmSetLockout = LOW;                                         // reset the lockout for the turn on function
      AlarmSetLockoutCounter = 0;                                    //Dump for next time
    }
  }

}  // end Void Loop






bool Sec27ASetOn() {
  Serial.println("Request to Set Burglar Alarm received SW #1 On...");

  ProxyRequestText = "Alexa or Local Web Set Request";
  RotateProxyLogArray();

  if (Sec27ASetState == HIGH) {  // only pulse relay if Burglar Alarm is currently Unset
    //sometimes alexa sends this request again about 2 secs later which turned the alarm off again on the second request
    // we need to lockout multiple turn on requests that are received in quick succession
    // or maybe, just extend the pulse duration ? (was 1 sec) - didnt work...
    if (AlarmSetLockout == LOW) {  // only allows set routine to run once, initally needed the alarm off request to release this
      // but this gave rise to problems if the alarm was set via alexa and reset via keypads or RF remote.
      // changed to reset automatically after 5 secs

      Serial.println("Burglar Alarm is Unset - pulsing relay to Set it");
      AlarmSetLockout = HIGH;  // set the lockout

      Serial.println("XXX Pulsing Relay on ...");

      // Turn on #1 Relay
      delay(10);
      Serial.write(rel1ON, sizeof(rel1ON));
      delay(10);
      Serial.println("Turning Relay#1 On ...");
      ProxyRequestText = "Set Request Honored";
      RotateProxyLogArray();

      // Turn on #1 Relay
      delay(10);
      Serial.write(rel1ON, sizeof(rel1ON));
      delay(10);
      Serial.println("Turning Relay#1 On ...");

      delay(1500);
      ;

      Serial.println("XXX Pulsing Relay off again ...");  // this makes a pulse which is what the security system wants

      // Turn off #1 Relay
      delay(10);
      Serial.write(rel1OFF, sizeof(rel1OFF));
      delay(10);
      Serial.println("Turning Relay#1 Off ...");
      //ProxyRequestText = "Relay 1 pulsing off - set";
      //RotateProxyLogArray();

      // Turn off #1 Relay
      delay(10);
      Serial.write(rel1OFF, sizeof(rel1OFF));
      delay(10);
      Serial.println("Turning Relay#1 Off ...");
    }
  } else {
    Serial.println("27A Security is already Set - not pulsing relay!");
    ProxyRequestText = "Set Request NOT Honored, already set";
    RotateProxyLogArray();
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
  ProxyRequestText = "Alexa or Local Web Unset Request";
  RotateProxyLogArray();

  if (Sec27ASetState == LOW) {  // only pulse relay if Burglar Alarm is currently Set
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
    ProxyRequestText = "UnSet Request Honored";
    RotateProxyLogArray();

    // Turn on #1 Relay
    delay(10);
    Serial.write(rel1ON, sizeof(rel1ON));
    delay(10);
    Serial.println("Turning Relay#1 On ...");

    delay(1500);// 1.5 sec pulse
    ;

    Serial.println("XXX Pulsing Relay off again ...");  // this makes a pulse which is what the security system wants

    // Turn off #1 Relay
    delay(10);
    Serial.write(rel1OFF, sizeof(rel1OFF));
    delay(10);
    Serial.println("Turning Relay#1 Off ...");
    //ProxyRequestText = "Pulsing relay 1 off - unset";
    //RotateProxyLogArray();

    // Turn off #1 Relay
    delay(10);
    Serial.write(rel1OFF, sizeof(rel1OFF));
    delay(10);
    Serial.println("Turning Relay#1 Off ...");
  } else {
    Serial.println("27A Security is already Unset, not pulsing relay...");
    ProxyRequestText = "UnSet Request NOT Honored - already Unset";
    RotateProxyLogArray();
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
  ProxyRequestText = "Alexa Panic Request";
  RotateProxyLogArray();
  // Turn on #2 Relay
  delay(10);
  Serial.write(rel2ON, sizeof(rel2ON));
  delay(10);
  Serial.println("Turning Relay#2 On ...");
  //ProxyRequestText = "Relay 2 pulsing on - panic";
  //RotateProxyLogArray();

  // Turn on #2 Relay
  delay(10);
  Serial.write(rel2ON, sizeof(rel2ON));
  delay(10);

  delay(1500); // 1.5 sec pulse

  // Turn off #2 Relay
  delay(10);
  Serial.write(rel2OFF, sizeof(rel2OFF));
  delay(10);
  Serial.println("Turning Relay#2 Off ...");
  //ProxyRequestText = "Relay 2 pulsing off - panic";
  //RotateProxyLogArray();
  // Turn off #2 Relay
  delay(10);
  Serial.write(rel2OFF, sizeof(rel2OFF));
  delay(10);
  Serial.println("Turning Relay#2 Off ...");





  isSec27APanicOn = false;
  return isSec27APanicOn;
}

bool Sec27APanicOff() {

  Serial.println("Request to stop Panic Mode received (SW#3 Off)");
  ProxyRequestText = "Alexa Panic Off Request";
  RotateProxyLogArray();

  // Turn off #2 Relay
  delay(10);
  Serial.write(rel2OFF, sizeof(rel2OFF));
  delay(10);
  Serial.println("Turning Relay#2 Off ...");
  //ProxyRequestText = "Relay 2 pulsing off - panic";
  //RotateProxyLogArray();
  // Turn off #2 Relay
  delay(10);
  Serial.write(rel2OFF, sizeof(rel2OFF));
  delay(10);
  Serial.println("Turning Relay#2 Off ...");


  isSec27APanicOn = false;
  return Sec27ASetState;
}

// connect to wifi – returns true if successful or false if not
boolean connectWifi() {
  boolean state = true;
  int i = 0;

  WiFi.mode(WIFI_STA);


  // Configures static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
    // this locked in ip address made the clock sync fail untill i added the DNS bits
    // if this is an issue, fix by locking mac address to ip address in the router config instead (not done as at June2026)
  }
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("Connecting to WiFi Network");

  // Wait for connection
  Serial.print("Connecting ...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(5000);
    Serial.print(".");
    if (i > 10) {
      state = false;
      break;
    }
    i++;
  }

  if (state) {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
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
  Serial.print("Requesting POST to Proxy ");
  Serial.println(VBNumberString);

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
/*
// this gave problems as the data transmitted had no leading zero and the watchdog falsely matched it with values in the 30 range
  client.println("Host: ProxyRequest" + (String(VBNumber)));  // this endpoint value gets to the server and is used to transfer the identity of the calling slave
  Serial.println("Host: ProxyRequest" + (String(VBNumber)));  //
*/
  client.println("Host: ProxyRequest" + VBNumberString);  // this endpoint value gets to the server and is used to transfer the identity of the calling slave
  Serial.println("Host: ProxyRequest" + VBNumberString);  // send to serial port as well
  client.println("Accept: */*");                              // this gets to the server!
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.println(data.length());
  client.println();
  client.print(data);

  delay(500);  // Can be changed
  if (client.connected()) {
    client.stop();  // DISCONNECT FROM THE SERVER
  }
  Serial.println();
  Serial.println("closing connection");
  delay(1000);
}  // end ProxyPost

void WatchDogPost() {

  TwitchLED();

  // assumes VBNumber set to desired VB call to be made
VBNumber = 40;  // 27A security watchdog code
  // 40 is Burglar Alarm watchdog

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
  Serial.println("VB button"+(String(VBNumber))+" request sent");
  client.println("Host: WatchDog Endpoint" + (String(VBNumber)));  // this endpoint value gets to the server and is used to transfer the identity of the calling slave
  client.println("Accept: */*");                                   // this gets to the server!
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.println(data.length());
  client.println();
  client.print(data);

  delay(500);  // Can be changed
  if (client.connected()) {
    client.stop();  // DISCONNECT FROM THE SERVER
  }
  Serial.println();
  Serial.println("closing connection");
  delay(1000);
}  // end WatchDogPost

void TwitchLED() {

  pinMode(GPIO2, OUTPUT);  // switch to an output

  digitalWrite(GPIO2, LOW);
  delay(10);
  digitalWrite(GPIO2, HIGH);

  //pinMode(GPIO2, INPUT); // switch back to an input
}  // end TwitchLED

void SetTime() {
  /* original code
  SetTimeWasSuccesfull = 0;

  Serial.println(" trying to synch clock...");
  // Synchronize time using SNTP. This is necessary to verify that
  // the TLS certificates offered by the server are currently valid. Also used by the graph plotting routines
  Serial.println("Setting time using SNTP");
  configTime(-13 * 3600, DSTOffset, "pool.ntp.org", "time.nist.gov");  // set localtimezone, DST Offset, timeservers, timeservers...


  time_t now = time(nullptr);
  //while (now < 8 * 3600 * 2) { // would take 8 hrs to fall thru. Thats a long time....
  while (now < 100) {
    delay(200);
    Serial.print(".");
    Serial.print(String(now));
    now = time(nullptr);
  }
  Serial.println("");
*/

// AL Suggestion


  SetTimeWasSuccesfull = 0;

  Serial.println(" trying to synch clock...");
  Serial.println("Setting time using SNTP");
  configTime(-13 * 3600, DSTOffset, "pool.ntp.org", "time.nist.gov");


  time_t now = time(nullptr);
  int SNTPtimeoutCounter = 0; // 1. Add a counter to track attempts

  // Loop runs while time is invalid AND we haven't hit the 10-second timeout (50 * 200ms)
  while (now < 100 && SNTPtimeoutCounter < 50) { 
    delay(200);
    Serial.print(".");
    Serial.print(String(now));
    now = time(nullptr);
    SNTPtimeoutCounter++; // Increment counter
  }
  Serial.println("");

  // 2. LOG THE FAILURE OR SUCCESS HERE
  if (now < 100) {
  ProxyRequestText = "RTC ReSync FAILED - SNTP Timeout After "  + String((SNTPtimeoutCounter)+1) + " Attempts";
  RotateProxyLogArray();
    Serial.println("Error: Failed to resync time. SNTP timeout reached.");
    SetTimeWasSuccesfull = 0; 
  } else {
    Serial.println("Success: Time synchronized successfully!");
  ProxyRequestText = "RTC ReSync Success after " + String((SNTPtimeoutCounter)+1) + " Attempts";
  RotateProxyLogArray();
    SetTimeWasSuccesfull = 1;
  }

  struct tm* timeinfo;  //http://www.cplusplus.com/reference/ctime/tm/
  time(&now);
  timeinfo = localtime(&now);
  Serial.println(timeinfo->tm_mon);
  Serial.println(timeinfo->tm_mday);
  Serial.println(timeinfo->tm_hour);
  Serial.println(timeinfo->tm_min);
  Serial.println(timeinfo->tm_sec);
  currentseconds = timeinfo->tm_sec;
  currentminutes = timeinfo->tm_min;
  currenthours = timeinfo->tm_hour;
  currentday = timeinfo->tm_mday;
  currentmonth = timeinfo->tm_mon;
  currentmonth = currentmonth + 1;  // month counted from 0
  currentday = currentday + 1;      // day counted from 0 (I think)
  currenthours = currenthours + 1;  // it was an hour out in testing , might actually be DST issue, IDK

  //ProxyRequestText = "RTC ReSync Success";
  //RotateProxyLogArray();

}  // End SetTime

void RotateProxyLogArray() {


  // Rotates ProxyLogArray[64] one slot to the left (toward index 0)


  for (ProxyLogArrayIndex = 0; ProxyLogArrayIndex < 64; ProxyLogArrayIndex = ProxyLogArrayIndex + 1) {

    ProxyLogArray[ProxyLogArrayIndex] = ProxyLogArray[(ProxyLogArrayIndex + 3)];
  }
  //Populate new log entry
  ProxyLogArray[57] = (String(currentday) + " / " + String(currentmonth));
  ProxyLogArray[58] = (String(currenthours) + ":" + String(currentminutes) + ":" + String(currentseconds));
  ProxyLogArray[59] = (String(ProxyRequestText));
  TwitchLED();

}  // end RotateProxyLogArray
