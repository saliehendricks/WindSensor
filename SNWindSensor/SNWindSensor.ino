/*
 Name:		SNWindSensor.ino
 Created:	1/25/2017 10:28:43 PM
 Author:	salie
*/

//#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <WifiRestClient.h>
#include <ArduinoOTA.h>
#include <math.h> 

//wind direction vals
int VaneValue;// raw analog value from wind vane 
int Direction;// translated 0 - 360 direction 
int CalDirection;// converted value with offset applied 
int LastValue;
int Offset = 0; 

//wind speed vals
#define WindSensorPin (4) // The pin location of the anemometer sensor D2
unsigned long Rotations; // cup rotation counter used in interrupt routine 
unsigned long ContactBounceTime; // Timer to avoid contact bounce in interrupt routine 
float WindSpeed; // speed miles per hour
String heading = "";
bool shouldSampleWind = true;
unsigned long lastSampleTime = 0;
unsigned long currentMills = 0;

//web communications
const char* pubnubUrl = "pubsub.pubnub.com";
//String push1 = "/publish/pub-c-ec980337-936e-49d4-9c31-0d4e54f3a784/sub-c-4332eecc-ba37-11e5-8622-0619f8945a4f/0/";
String push1 = "/publish/pub-c-b773f23b-fb3f-4570-bd59-fa77d229f482/sub-c-3dea2048-cb21-11e5-b684-02ee2ddab7fe/0/snwind.snw1/0/";
String restcall = "";
int lastSent = 0;
bool isPubNub = true;
String timetoken = "";
int publishInterval = 15000;
ESP8266WebServer server(80);

// WiFi parameters
char* internal_ssid = "NURIO";
char* internal_password = "12345";

char* device_id = "snw1";

void setup() {
	LastValue = 1;
	Serial.begin(115200);

	Serial.println("Config starting...");
	//
	EEPROM.begin(512);//2 x 4bytes (for offset which can be -359 to +359, EEPROM stores only 255 per byte) + 4bytes for the publish interval 0-255 seconds

	int offsetMem1 = 361;
	offsetMem1 = EEPROM.read(0);
	int offsetMem2 = 361;
	offsetMem2 = EEPROM.read(1);

	int pubsubintervalMem = -1;
	pubsubintervalMem = EEPROM.read(3);

	Serial.print("EEPROM stored: Offset = "); Serial.print(offsetMem1 + offsetMem2); Serial.print(" publish interval = "); Serial.println(pubsubintervalMem);
	//if the memory values are valid set the variables
	if (offsetMem1 + offsetMem2 > -359 & offsetMem1 + offsetMem2 < 361) {
		Offset = offsetMem1 + offsetMem2;
		Serial.println("EEPROM restored offset = " + (String)Offset);
	}
	
	//more than 5 seconds and less than 5mins
	if (pubsubintervalMem > 5 & pubsubintervalMem < 600) {
		publishInterval = pubsubintervalMem * 1000; //convert to milliseconds
		Serial.println("EEPROM restored publish interval = " + (String)publishInterval);
	}
	

	//Wifi setup
	WiFiManager wifi;
	wifi.autoConnect("SNWind");
	Serial.println("Connected to wifi ok)");

	//wifiServer.begin();
	//Serial.println("Web Server started");
	Serial.print("Chip ID: ");;
	Serial.println(String(ESP.getChipId(), HEX));


	//Sensor setup
	pinMode(WindSensorPin, INPUT);
	attachInterrupt(digitalPinToInterrupt(WindSensorPin), rotation, FALLING);
	//Serial.println("Vane Value\tDirection\tHeading");

	//Setup OTA for wireless firmware upgrades
	ArduinoOTA.onStart([]() {
		Serial.println("Start OTA");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd OTA");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.setHostname(device_id);
	ArduinoOTA.begin();
	Serial.println("OTA Ready");
	Rotations = 0;

	pinMode(LED_BUILTIN, OUTPUT);

	//Web Server
	if (MDNS.begin("snwind1")) {
		Serial.println("MDNS responder started");
	}
	
	server.on("/", handleRoot);
	server.on("/set", handleSetArg); ///set?offset=25
	server.onNotFound(handleNotFound);
	server.begin();
	Serial.println("HTTP server started");

}

void loop() {

	server.handleClient();
	
	//WIND SPEED
	//Rotations = 0; // Set Rotations count to 0 ready for calculations 
	currentMills = millis();

	// convert to mp/h using the formula V=P(2.25/T) 
	// V = P(2.25/3) = P * 0.75   * 1.609344 to km/h 
	if (lastSampleTime == 0 | currentMills - lastSampleTime > 3000)
	{
		//WindSpeed = Rotations * 1.207008;
		WindSpeed = Rotations * 2.25/(currentMills/1000 - lastSampleTime/1000 )  * 1.609344;
		Serial.print(Rotations); Serial.print("\t\t"); Serial.print(currentMills - lastSampleTime); Serial.print("\t\t");
		Serial.println(WindSpeed);
		lastSampleTime = millis();
		Rotations = 0;

		
	}

	//WIND DIRECTION
	VaneValue = analogRead(A0);
	Direction = map(VaneValue, 0, 1023, 0, 360);
	CalDirection = Direction + Offset;

	if (CalDirection > 360)
		CalDirection = CalDirection - 360;

	if (CalDirection < 0)
		CalDirection = CalDirection + 360;

	//// Only update the display if change greater than 25 degrees. 
	//if (abs(CalDirection - LastValue) > 25)
	//{
	//	Serial.print(VaneValue); Serial.print("\t\t");
	//	Serial.print(CalDirection); Serial.print("\t\t");
	//	getHeading(CalDirection);
	//	LastValue = CalDirection;
	//}

	if (isPubNub & (currentMills - lastSent > publishInterval | lastSent == 0)) {
		
		Serial.print(VaneValue); Serial.print("\t\t");
		Serial.print(CalDirection); Serial.print("\t\t");

		digitalWrite(LED_BUILTIN, LOW);
		
		
		WiFiRestClient restClient(pubnubUrl);
		restcall = push1 + "%7B%22id%22%3A%22" + device_id + "%22%2C%22t%22%3A%22" + timetoken + "%22%2C%22kmh%22%3A" + WindSpeed + "%2C%22dir%22%3A%22" + heading + "%22%2C%22bear%22%3A" + CalDirection + "%7D";

		Serial.println("sending to pubnub:" + restcall);
		String response = "";
		int statusCode = restClient.get(restcall.c_str(), &response);
		Serial.print("response from pubnub: "); Serial.print(response);
		Serial.println(statusCode);
		response.replace("]", "");
		response.replace(" ", "");
		response.replace("\"", "");
		timetoken = response.substring(response.lastIndexOf(",")+1);
		//timetoken = response;

		lastSent = millis();
		digitalWrite(LED_BUILTIN, HIGH);
		
	}
	
}

// Converts compass direction to heading 
void getHeading(int direction) {
	if (direction < 22) 
	{
		heading = "N";
		Serial.println("N");
	}		
	else if (direction < 67)
	{
		heading = "NE";
		Serial.println("NE");
	}
	else if (direction < 112)
	{
		heading = "E";
		Serial.println("E");
	}
	else if (direction < 157) 
	{
		heading = "SE";
		Serial.println("SE");
	}
	else if (direction < 212)
	{
		heading = "S";
		Serial.println("S");
	}
	else if (direction < 247) 
	{
		heading = "SW";
		Serial.println("SW");
	}
		
	else if (direction < 292)
	{
		heading = "W";
		Serial.println("W");
	}
	else if (direction < 337) 
	{
		heading = "NW";
		Serial.println("NW");
	}		
	else 
	{
		heading = "N";
		Serial.println("N");
	}
		
}

// This is the function that the interrupt calls to increment the rotation count 
void rotation() {

	if ((millis() - ContactBounceTime) > 15) { // debounce the switch contact. 
		Rotations++;
		ContactBounceTime = millis();
	}

}


void handleRoot() {
	digitalWrite(LED_BUILTIN, LOW);
	String homepage = "<div style='font-family:Tahoma, Sans-Serif'><img src='http://sncdn.com/res/img/homepage/safarinow.png'> <br/><br/> SNWind Ready! <br/><br/> Set offset (-360 to 360):<input type=\"number\" id=\"offset\" min='-360' max='360' value='" + (String)Offset;
	homepage += "'/><a href='#' onclick=\"window.open('/set?offset=' + document.getElementById('offset').value); \"> save</a>  <br/><br/>    Publish data every:<input type=\"number\" id=\"pubtime\" min='5' max='255' value='" + (String)(publishInterval/1000);
	homepage += "'/><span> seconds</span>  <a href='#' onclick=\"window.open('/set?pubtime=' + document.getElementById('pubtime').value); \">save</a></div>";
	server.send(200, "text/html", homepage);
	digitalWrite(LED_BUILTIN, HIGH);	
}

void handleNotFound() {
	digitalWrite(LED_BUILTIN, LOW);
	String message = "Oops, Page Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";
	for (uint8_t i = 0; i<server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	server.send(404, "text/plain", message);
	digitalWrite(LED_BUILTIN, HIGH);
}

void handleSetArg() {

	String message = "";
	if (server.arg("offset").length() > 0)
	{   
		//Parameter found
		String offSetparam = server.arg("offset");
		Serial.println("offset requested:" + offSetparam);
		Offset = atoi(offSetparam.c_str());
		message = "Offset is now set to: " + (String)Offset;
		Serial.println(message);
		server.send(200, "text/plain", message);
		
		//we split the value by 2 and always store it half half so that it doesnt overflow
		if (Offset % 2 == 0) {
			EEPROM.write(0, Offset / 2);
		}
		else {
			EEPROM.write(0, Offset / 2 + 1);
		}
		EEPROM.write(1, Offset / 2);
		EEPROM.commit();
		message = "EEPROM offset commited - " + (String)Offset;
		Serial.println(message);
		
	}
	else if (server.arg("pubtime").length() > 0)
	{
		//Parameter found
		String param = server.arg("pubtime");
		Serial.println("param requested:" + param);
		publishInterval = atoi(param.c_str()) * 1000;
		message = "pubtime is now set to: " + (String)(publishInterval/1000);
		Serial.println(message);
		server.send(200, "text/plain", message);

		EEPROM.write(2, publishInterval);
		EEPROM.commit();

		message = "EEPROM publish time commited - " + (String)publishInterval;
		Serial.println(message);
	}
	else 
	{     
		message = "Not settings updated, Offset: " + (String)Offset + " pubtime (sec):" + (String)publishInterval;
		server.send(200, "text/plain", message);          //Returns the HTTP response
	}
	
}