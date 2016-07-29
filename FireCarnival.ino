/*
    May 2016 (c) neil verplank
    
    Use the Actuator to receive "poof" commands via a wireless network
    (specifically, the server running on a Raspberry Pi 3 configured as an AP
    with a static IP).
    
    In a nutshell, hook up "BAT" and ground to a 5V power supply, hookup pin
    12 (and/or 13, and/or 14) to the relay (which needs it's own power! Huzzah
    can't drive the relay very well if at all).  IF you want to send high score
    or some other success to the server, connect pin 16 to ground (via a button,
    or have the game Arduino use one of its relays to short 16 to ground
    momentarily).
    
*/

#include <ESP8266WiFi.h>

//--------------- Modify These Per Unit Specifications -------------------------------
#define WHOAMI "DRAGON"               // which game am I?
#define REPORT  "score"              // what happened?
#define DEBUG   1                   // 1 to print useful messages to the serial port
#define REDLED  0
#define BLUELED  2

const boolean HASBUTTON   = 1;      // Does this unit have a button on
int           inputButton = 5;      // What pin is that button on?

// Huzzah pins to "fire off" when poofing (hook these to the relays)
int         allSolenoids[]     = {12,13,14};
//------------ Collect unique variables here ---------- ------------------------------


char ssid[] =  "CapnNemosCarnival"; // your network SSID (name)
char pass[] =  "raspberry";         // your network password (use for WPA, or use as key for WEP)
#define HOST   "192.168.100.1"      // your server ip
#define PORT    5061                // server port

const boolean RELAY_ON       = 0;     // opto-isolated arrays aRE active LOW
const boolean RELAY_OFF      = 1;

WiFiClient  client;

int stillOnline             = 0;  // Check for connection timeout every 5 minutes
int maxPoof                 = 0;  // poofer timeout
int magic                   = 0;  // counter for magic code
int notmagic                = 0;  // timeout for magic code
int beat                    = 0;  // beat counter in magic code


int         keyIndex         = 0;      // your network key Index number (needed only for WEP)
int         status           = WL_IDLE_STATUS;
boolean     alreadyConnected = false;  // first time through?
int         looksgood        = 0;      // still connected (0 is not)
boolean     poofing          = 0;      // poofing state (0 is off)
boolean     notified         = true;  // notify once the poofers are off
int         solenoidCount    = sizeof(allSolenoids)/sizeof(int);


void setup() {

    pinMode(REDLED, OUTPUT);    // set up LED
    pinMode(BLUELED, OUTPUT);    // set up Blue LED
    digitalWrite(0, HIGH); // and turn it off

    pinMode(inputButton, INPUT_PULLUP);   
   
    for (int x = 0; x < solenoidCount; x++) {
        pinMode(allSolenoids[x], OUTPUT);
        digitalWrite(allSolenoids[x], RELAY_OFF);
    }
    

    //Initialize serial and wait for port to open:
    if (DEBUG) { Serial.begin(115200); }
    // attempt to connect to Wifi network:
    connectWifi();
    // you're connected now, so print out the status:
    if (DEBUG) { printWifiStatus(); }
}


/*
   We're looping, looking for signals from the server to poof (or do something)
   or signals from the game to send to the server (high score!).
   
   Basically, we check every 150 milliseconds if we're still poofing (msg incoming)
   still not poofing (no message)

*/
void loop() {
 
    // CONFIRM CONNECTION
    int CA = 0;
    
    if (!client) {
       debugMsg("Establishing connection");
       looksgood = reconnect();
    }else{ // turn on status light to show unit is online
       if (DEBUG) { digitalWrite(0, LOW); }
    }
    

    // process any incoming messages
    
    if (client && looksgood) {
        // FIRST TIME AROUND
        
        if (!alreadyConnected) {
            // clean out the input buffer:
            client.flush();
            debugPiece(WHOAMI);
            debugMsg(": ONLINE");
            client.println(WHOAMI); // Tell server which game
            alreadyConnected = true;
        }
        
        // there's an incoming message - read one and loop (ignore non-phrases)
        
        CA =  client.available();
        if (CA) {

            // read incoming stream a phrase at a time.
              int x=0;
              int cpst;
              char incoming[100];
              memset(incoming,NULL,100);
              char rcvd = client.read();

              // read until start of phrase (clear any cruft)
              while (rcvd != '$' && x< CA) { rcvd = client.read(); x++;}
              
              if (rcvd=='$') { /* start of phrase */
                 int n=0;
                 flashBlue(1, 15, true); // show communication
                 while (rcvd=client.read()) {
                   if(rcvd=='%') { /* end of phrase - do something */
                     cpst = strcmp(incoming, "poofall=1");
                     if(cpst == 0) { /* strings are `equal */
                      maxPoof++;
                      if(!poofing && maxPoof <= 30) { // if we're not poofing or at the limit
                        poofing = 1;
                        maxPoof++;
                        digitalWrite(0,LOW);  // turn light on
                        notified = false;     // let us know when the poofer stops
                        poofAll(true);
                      }else if(maxPoof > 30){
                        poofing = 0;
                      }
                    } else {
                      // it's some other phrase
                      poofing = 0;
                      debugMsg(incoming);
                      debugMsg("other phrase, poofing 0");
                    }
                     break; // phrase read, end loop
                   } // end if '%' :: end of phrase
                   else { // not the end of the phrase
                       incoming[n]= rcvd;
                       n++;
                   }
                 } // end while client.read
              } // end if $ beginning of phrase
        } // end if  client available
        else {
          // no message, confirm not poofing
          maxPoof = 0;
          poofing = 0;
        }
    } // end client looksgood
    else { // no connection, stop poofing
      poofing = 0;
      
    }
    
    if (poofing == 0) {
        digitalWrite(0,HIGH); // done poofing, turn off lights
        // turn off all poofers
        poofAll(false);
        if(notified == false){ // reset notifications and magic.
          if(magic == 0 ){
            notmagic = 0;
            beat = 0;
          }
          notified = true;
          magic++;
        }
    }

    if (client.available()) { // Is the server asking for something?
      delay(5);
      } else {                    // If not, run offline program
      if(magic > 0){          // if the button has been pressed start magic counter
        notmagic++;
      }
      if(notmagic >= 30){     // No magic, start over
        notmagic = 0;
        magic = 0;
        beat = 0;
        debugMsg("Magic cleared");
      }else if(magic == 4){
        beat++;               // did they play the ditty?
      }else if(magic >= 6 && beat > 3){
        poofStorm();          // release the quirky fury!!
        notmagic = 0;
        magic = 0;
      }
      if(HASBUTTON == 1){     // If this unit reacts to
        checkScore();
      }
      delay(125);  
    }
    if(DEBUG){
      stillOnline++;
        if(stillOnline >= 960){
          debugMsg("Connection Maintained");
          printWifiStatus();  //are we still online?
          stillOnline = 0;
          flashBlue(2, 166, true); // connection maintained
        }
    }
   

}

/*================= END MAIN LOOP== =====================*/


int checkScore(){
    boolean shotValue=digitalRead(inputButton);
    if(shotValue==LOW) {
        // button being pushed
        debugMsg("Button pushed!");
        callServer(REPORT);
    }
}

/*================= WIFI FUNCTIONS =====================*/

int reconnect() {
    // wait for a new client:
    // Attempt a connection with base unit
    if (!client.connect(HOST, PORT)) {
        debugMsg("connection failed");
        digitalWrite(2,HIGH); //turn off blue connection light
        flashRed(5,25,false);  // flash 5 times to indicate failure
        return 0;
    } else {
      digitalWrite(2,LOW); //turn on blue connection light
      return 1;
    }
}


void printWifiStatus() {
 
    // print the SSID of the network you're attached to:
    if (DEBUG){
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
    }
    // print your WiFi shield's IP address:
    IPAddress ip = WiFi.localIP();
    if (DEBUG) {
      Serial.print("IP Address: ");
      Serial.println(ip);
    }
    // print the received signal strength:
    long rssi = WiFi.RSSI();
    if (DEBUG) {
      Serial.print("signal strength (RSSI):");
      Serial.print(rssi);
      Serial.println(" dBm");
    }
}

void connectWifi(){
  while ( status != WL_CONNECTED) {
        debugPiece("Attempting to connect to SSID: ");
        debugMsg(ssid);
        // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
        status = WiFi.begin(ssid, pass);
        // flash and wait 10 seconds for connection:
        flashBlue(15, 166, false); // flash 5 times slowly to indicate connecting
    }
}

void callServer(String message){
  client.print(WHOAMI); // Tell server which game:message
  client.print(':');
  client.println(message);
  flashBlue(5, 30, true); // show communication
}

/*================= DEBUG FUNCTIONS =====================*/

void flashRed(int times, int rate, bool finish){
  // flash `times` at a `rate` and `finish` in which state
  for(int i = 0;i <= times ;i++){
    digitalWrite(REDLED, finish);
    delay(rate);
    digitalWrite(REDLED, !finish);
    delay(rate);
  }
}

void flashBlue(int times, int rate, bool finish){
  // flash `times` at a `rate` and `finish` in which state
  for(int i = 0;i <= times ;i++){
    digitalWrite(BLUELED, finish);
    delay(rate);
    digitalWrite(BLUELED, !finish);
    delay(rate);
  }
}

void debugMsg(String message){
  if (DEBUG) { Serial.println(message); }
}

void debugPiece(String message){
  if (DEBUG) { Serial.print(message); }
}

/*================= POOF FUNCTIONS =====================*/

void poofAll(boolean state) {
  // turn on all poofers
  for (int y = 0; y < solenoidCount; y++) {
    if(state == true){
      digitalWrite(allSolenoids[y], RELAY_ON);
    }else{
      digitalWrite(allSolenoids[y], RELAY_OFF);
    }
  }
  if(state == true){ // optional messaging
    debugMsg("Poofing started");
  }else{
    if(!notified){
      debugMsg("Poofing stopped");
    }
  }
}

void poofRight(boolean state) {
   for (int y = 0; y < solenoidCount; y++) {
      if(state == true){
        digitalWrite(allSolenoids[y], RELAY_ON);
        delay(200);
      }else{
        digitalWrite(allSolenoids[y], RELAY_OFF);
        delay(200);
      }
  }
}

void poofRight(boolean state, int rate) {  // overloaded function offering speed setting
   if(!rate){ rate = 200;}
   for (int y = 0; y < solenoidCount; y++) {
      if(state == true){
        digitalWrite(allSolenoids[y], RELAY_ON);
        delay(rate);
      }else{
        digitalWrite(allSolenoids[y], RELAY_OFF);
        delay(rate);
      }
  }
}

void poofLeft(boolean state) {
   for (int y = solenoidCount; y == 0;  y--) {
      if(state == true){
        digitalWrite(allSolenoids[y], RELAY_ON);
        delay(200);
      }else{
        digitalWrite(allSolenoids[y], RELAY_OFF);
        delay(200);
      }
  }
}

void poofLeft(boolean state, int rate) {  // overloaded function offering speed setting
   if(!rate){ rate = 200;}
   for (int y = solenoidCount; y == 0;  y--) {
      if(state == true){
        digitalWrite(allSolenoids[y], RELAY_ON);
        delay(rate);
      }else{
        digitalWrite(allSolenoids[y], RELAY_OFF);
        delay(rate);
      }
  }
}

void puffRight(boolean state, int rate) {
   if(!rate){ rate = 200;}
   for (int y = 0; y < solenoidCount; y++) {
      if(state == true){
        digitalWrite(allSolenoids[y], RELAY_ON);
        delay(rate);
      }else{
        digitalWrite(allSolenoids[y], RELAY_OFF);
        delay(rate);
      }
  }
}

void puffSingleRight(int rate) { // briefly turn on individual poofers
   if(!rate){ rate = 200;}
   for (int y = 0; y < solenoidCount+1; y++) {
    digitalWrite(allSolenoids[y], RELAY_ON);
    if(y > 0){
      digitalWrite(allSolenoids[y-1], RELAY_OFF);
    }
    delay(rate);
  }
}

void poofSingleLeft(int rate) { // briefly turn on all poofers to the left
  int arraySize = solenoidCount+1;
   if(!rate){ rate = 200;}
   for (int y = arraySize; y == 0;  y--) {
    digitalWrite(allSolenoids[y], RELAY_ON);
    if(y!= arraySize){
      digitalWrite(allSolenoids[y+1], RELAY_OFF);
    }
    delay(rate);
  }
}

void poofStorm(){  // crazy poofer display
  debugMsg("POOFSTORM");
  flashRed(10, 30, false); // indicate action
  poofAll(true);
  delay(300);
  poofAll(false);
  poofSingleLeft(200);
  delay(200);
  puffSingleRight(200);
  delay(200);
  poofLeft(true);
  poofAll(false);
  delay(200);
  poofRight(true);
  poofAll(false);
  delay(200);
  poofAll(true);
  delay(600);
  poofAll(false);
}

void gunIt(){ // rev the poofer before big blast
  for(int i = 0; i < 4; i++){
    poofAll(true);
    delay(100);
    poofAll(false);
    delay(50);
  }
  poofAll(true);
  delay(500);  
}

void poofEven(int rate){  // toggle even numbered poofers
  for (int y = 0; y < solenoidCount; y++) {
    if(y != 0 && y%2 != 0 ){
      digitalWrite(allSolenoids[y], RELAY_ON);
      delay(rate);
      digitalWrite(allSolenoids[y], RELAY_OFF);
      delay(rate);
    }
  }
}

void poofOdd(int rate){ // toggle odd numbered poofers
  for (int y = 0; y < solenoidCount; y++) {
    if(y == 0 || y%2 == 0 ){
      digitalWrite(allSolenoids[y], RELAY_ON);
      delay(rate);
      digitalWrite(allSolenoids[y], RELAY_OFF);
      delay(rate);
    }
  }
}

void lokiChooChoo(int start, int decrement, int rounds){ // initial speed, speed increase, times
  for(int i = 0; i <= rounds; i++){
    poofOdd(start);
    start -= decrement;
    poofEven(start);
    start -= decrement;
  }
  delay(start);
  poofAll(true);
  delay(start);
  poofAll(false);
}
