/*
 Name:		SNWindSensor.ino
 Created:	1/25/2017 10:28:43 PM
 Author:	salie
*/

//#include <DNSServer.h>
#include <ESP8266WiFi.h>
//#include <WiFiClient.h>
//#include <ESP8266WebServer.h>
//#include <ESP8266mDNS.h>
//#include <WiFiUDP.h>
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
#define Offset 0; 

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

// WiFi parameters
char* internal_ssid = "NURIO";
char* internal_password = "12345";

#define LISTEN_PORT           80
WiFiServer wifiServer(LISTEN_PORT);

char* device_id = "snw1";

void setup() {
	LastValue = 1;
	Serial.begin(115200);

	Serial.println("Config starting...");

	//Wifi setup
	WiFiManager wifi;
	wifi.autoConnect("SNWind");
	Serial.println("Connected to wifi ok)");

	wifiServer.begin();
	Serial.println("Web Server started");
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

}

void loop() {
	//WIND SPEED
	//Rotations = 0; // Set Rotations count to 0 ready for calculations 
	currentMills = millis();

	
	//sei(); // Enables interrupts 
	//delay(3000); // Wait 3 seconds to average 
	//cli(); // Disable interrupts 

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

	// Only update the display if change greater than 2 degrees. 
	if (abs(CalDirection - LastValue) > 5)
	{
		Serial.print(VaneValue); Serial.print("\t\t");
		Serial.print(CalDirection); Serial.print("\t\t");
		getHeading(CalDirection);
		LastValue = CalDirection;
	}

	if (isPubNub & (currentMills - lastSent > 10000 | lastSent == 0)) {
		
		//delay(5000);
		/*WiFiClient client;
		const int httpPort = 80;
		if (!client.connect(pubnubUrl, httpPort)) {
			Serial.println("pubnub connection failed");
			return;
		}*/
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

		//client.print(String("GET ") + restcall + " HTTP/1.1\r\n" +
		//	"Host: " + pubnubUrl + "\r\n" +
		//	"Connection: close\r\n\r\n");
		//unsigned long timeout = millis();
		//while (client.available() == 0) {
		//	if (millis() - timeout > 5000) {
		//		Serial.println(">>> Client Timeout !");
		//		client.stop();
		//		return;
		//	}
		//}

		//// Read all the lines of the reply from server and print them to Serial
		//while (client.available()) {
		//	String line = client.readStringUntil('\r');
		//	Serial.print(line);
		//}
		lastSent = millis();
		//yield();
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

