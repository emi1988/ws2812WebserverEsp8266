//---------------------------------------------------------------------
//ws2812Webserver
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>

#include <TimeLib.h>
#include <time.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>               //I2C library
#include <RtcDS3231.h>    //RTC 
#include <ArduinoOTA.h>


#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

const char* host = "esp8266-webupdate";


#include "FS.h"			//file system
#include <ArduinoJson.h>

#include "wifiPasswords.h"
#include "defs.h"

//ESP8266WebServer httpServer(80);
//ESP8266HTTPUpdateServer httpUpdater;



#ifdef __AVR__
#include <avr/power.h>
#endif



#define BGTDEBUG 1

//LED-Stripe
#define PIN 12
//pin 4 == lua d2

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(16, PIN, NEO_GRB + NEO_KHZ400);


//rtc and time stuff
RtcDS3231<TwoWire> rtc(Wire);

WiFiUDP ntpUDP;

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);


//---------------------------------------------------------------------
// WiFi

byte my_WiFi_Mode = 0;  // WIFI_STA = 1 = Workstation  WIFI_AP = 2  = Accesspoint

IPAddress ip(192, 168, 0, 99);  //Node static IP
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);


WiFiServer server(80);
// Start a TCP Server on port 5045
WiFiServer tcpServer(5045);

WiFiClient client;
WiFiClient m_tcpClient;

#define MAX_PACKAGE_SIZE 2048
char HTML_String[5000];
char HTTP_Header[150];

//---------------------------------------------------------------------
// Allgemeine Variablen

int Aufruf_Zaehler = 0;

#define ACTION_OK 1
#define ACTION_NOTOK 2
#define ACTION_SET_DATE_TIME 3
#define ACTION_DEAKTIVATE_ALARM 4
#define ACTION_LIES_AUSWAHL 5
#define ACTION_LIES_VOLUME 6
#define ACTION_SET_LIGHT 7
#define ACTION_SET_LIGHTCHANGE 8

int action;

// Vor- Nachname
//char Vorname[20] = "B&auml;rbel";
//char Nachname[20] = "von der Waterkant";

// Uhrzeit Datum
byte Uhrzeit_HH = 16;
byte Uhrzeit_MM = 47;
byte Uhrzeit_SS = 0;
byte Datum_TT = 9;
byte Datum_MM = 2;
int Datum_JJJJ = 2016;

bool alarmIsSet = false;
bool dimFinished = true;


int m_currentState = -1;
int m_colorChangeMode;

int alarmColorRed, alarmColorGreen, alarmColorBlue;
double m_curretColorValue[3] = { 0, 0, 0 };


uint32_t m_lastDimTime = 0;
unsigned long m_lastEffectTime = 0;

int m_lastEffectStep = 0;

int m_dimSteps = 60;
int m_currentDimStep = 0;

// dimDuration / steps
uint32_t dimIntervall;
uint32_t dimDuration = 120;
uint32_t m_lightChangeDuration = 1000;

int lightChangeMode;


// checkboxen
char Wochentage_tab[7][3] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
byte Wochentage = 0;

// Radiobutton
char Jahreszeiten_tab[4][15] = { "Fr&uuml;hling", "Sommer", "Herbst", "Winter" };
byte Jahreszeit = 0;

// Combobox
char Wetter_tab[4][10] = { "Sonne", "Wolken", "Regen", "Schnee" };
byte Wetter;


// Slider
byte Volume = 15;

int colorRed = 0;
int colorGreen = 0;
int colorBlue = 0;


char tmp_string[20];
//---------------------------------------------------------------------
void setup() {
#ifdef BGTDEBUG
	Serial.begin(115200);
	/*
	for (int i = 2; i > 0; i--) {
		Serial.print("Warte ");
		Serial.print(i);
		Serial.println(" sec");
		delay(1000);
	}
	*/
#endif

	//LED init
	strip.begin();
	strip.show(); // Initialize all pixels to 'off'

	//---------------------------------------------------------------------
	// WiFi starten

	scanWifiNetworks();


	// WiFi_Start_STA();
	// if (my_WiFi_Mode == 0) WiFi_Start_AP();

	// get and save the current date and time

	rtc.Begin();                     //Starts I2C


	setupNTPSync();

	//try to get timt from ntp

	for (int i = 0; i < 3; i++)
	{
		Serial.print("try to get time from ntp. try numer: ");
		Serial.println(i);


		if (setRtcTimeFromNTP() == 0)
		{
			break;
		}
		delay(500);
	}

	setSyncProvider(getRtcTime);

	timeClient.end();


	//web update server
	//MDNS.begin(host);

	//httpUpdater.setup(&httpServer);
	//httpServer.begin();

	//MDNS.addService("http", "tcp", 80);
	//Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);

}

//---------------------------------------------------------------------
void loop() {

	//getRtcTime();

	//web update server
	//httpServer.handleClient();

	//ArduinoOTA.handle();

	//String hostName = ArduinoOTA.getHostname();

	//Serial.print("ota hostname: ");
	//Serial.println(hostName);

	tcpTraffic();
	WebsiteTraffic();
	delay(1000);

	switch (m_currentState)
	{
	case state_wakupAlarm:

		if (alarmIsSet == true)
		{
			//compare current time with the alarm time
			uint8_t alarmHour, alarmMinute;
			getAlarmTime(&alarmHour, &alarmMinute);

			RtcDateTime currentTime = rtc.GetDateTime();    //get the time from the RTC
			Serial.print("current time: ");
			Serial.print(currentTime.Hour());
			Serial.print(":");
			Serial.println(currentTime.Minute());

			if (currentTime.Hour() == alarmHour & currentTime.Minute() == alarmMinute)
			{
				//first deactivate the alarmIsSet-trigger 
				alarmIsSet = false;

				dimFinished = false;
				Serial.println("ALARM triggered !!");

				m_lastDimTime = currentTime.Epoch64Time();

			}

		}

		if (dimFinished == false)
		{
			dimLightOn();
		}

		break;

	case state_lightEffect:
		//Serial.println("light effect mode selected");

		switch (m_colorChangeMode)
		{

		case 1:
		//	Serial.println("rainbow effect selected");
			rainbow(m_lightChangeDuration);

			break;

		case 2:
		//	Serial.println("rainbow cycle effect selected");
			rainbowCycle(m_lightChangeDuration);

			break;

		default:
			break;
		}


	default:
		break;
	}



}


void dimLightOn()
{

	RtcDateTime currentTime = rtc.GetDateTime();    //get the time from the RTC

	uint32_t currentTimeSinceEpoch = currentTime.Epoch64Time();

	if (currentTimeSinceEpoch > m_lastDimTime + dimIntervall)
	{
		m_lastDimTime = m_lastDimTime + dimIntervall;

		Serial.println("alarmColorRed / m_dimSteps:");
		Serial.println((double)alarmColorRed / (double)m_dimSteps);

		m_curretColorValue[0] = m_curretColorValue[0] + ((double)alarmColorRed / (double)m_dimSteps);
		m_curretColorValue[1] = m_curretColorValue[1] + ((double)alarmColorGreen / (double)m_dimSteps);
		m_curretColorValue[2] = m_curretColorValue[2] + ((double)alarmColorBlue / (double)m_dimSteps);


		Serial.println("set new  brightness values:");
		Serial.print("red: ");
		Serial.println(m_curretColorValue[0]);
		Serial.print("green: ");
		Serial.println(m_curretColorValue[1]);
		Serial.print("blue: ");
		Serial.println(m_curretColorValue[2]);



		if (m_currentDimStep > m_dimSteps)
		{
			dimFinished = true;

			m_currentDimStep = 0;

			m_curretColorValue[0] = 0;
			m_curretColorValue[1] = 0;
			m_curretColorValue[2] = 0;

			Serial.println("dimming finished:");

		}
		else
		{
			m_currentDimStep++;

			//set the led-stripe brightnes
			colorWipe(strip.Color((int)m_curretColorValue[0], (int)m_curretColorValue[1], (int)m_curretColorValue[2]), 20);
		}


	}

}


void scanWifiNetworks()
{
	// Set WiFi to station mode and disconnect from an AP if it was previously connected
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	delay(100);

	String ssidList[] = { wifiNetworks };
	String pwList[] = { wifiPassworts };


	Serial.println("scan start");

	// WiFi.scanNetworks will return the number of networks found
	int n = WiFi.scanNetworks();
	Serial.println("scan done");
	if (n == 0)
		Serial.println("no networks found");
	else
	{
		Serial.print(n);
		Serial.println(" networks found");
		for (int i = 0; i < n; ++i)
		{
			// Print SSID and RSSI for each network found
			Serial.print(i + 1);
			Serial.print(": ");
			Serial.print(WiFi.SSID(i));
			Serial.print(" (");
			Serial.print(WiFi.RSSI(i));
			Serial.print(")");
			Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
			delay(10);
		}

		int numberOfSsids = sizeof(ssidList) / sizeof(ssidList[0]);

		//check if one ssid is in our list

		Serial.println("check list for known SSIDs");

		for (int i = 0; i < n; ++i)
		{
			for (int j = 0; j < numberOfSsids; j++)
			{
				if (WiFi.SSID(i).compareTo(ssidList[j]) == 0)
				{
					Serial.println("found known SSID:");
					Serial.println(ssidList[j]);

					const char* ssid = ssidList[j].c_str();
					const char* pw = pwList[j].c_str();

					setupWifi(ssid, pw);

					return;
				}
			}
		}
	}

	// if no connection is established, generate an AP

	if (WiFi.status() != WL_CONNECTED)
	{
		WiFi.disconnect();

		WiFi.mode(WIFI_AP);


		Serial.print("no saved wifi network aviable so create AP");

		WiFi.softAP("wakeUpLight", "banane");

		WiFi.softAPConfig(ip, ip, subnet);

		IPAddress myIP = WiFi.softAPIP();
		Serial.print("AP IP address: ");
		Serial.println(myIP);

		Serial.println("Starting Server");
		server.begin();

		Serial.println("Starting TCP Server");
		tcpServer.begin();

	}

	// Start OTA server.
	//ArduinoOTA.setHostname("wakeUpConfig");
	//ArduinoOTA.begin();



	/* did not work
	if (!MDNS.begin("esp8266"))
	{
	Serial.println("Error setting up MDNS responder!");
	}
	else
	{
	MDNS.addService("http", "tcp", 80);

	Serial.println("mDNS responder started");
	}
	*/

}

void setupWifi(const char * ssid, const char* password)
{
	Serial.println("Connecting to ");
	Serial.print(ssid);

	WiFi.mode(WIFI_STA);

	bool retCode = WiFi.begin(ssid, password);

	Serial.println("Wifi begin:" + retCode);

	WiFi.config(ip, gateway, subnet);


	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("IP address assigned by DHCP: ");
	Serial.print(WiFi.localIP());

	//Serial.print(".");
	Serial.println("WiFi-Status: ");
	Serial.print(WiFi.status());

	Serial.println("Starting Web Server");
	server.begin();

	Serial.println("Starting TCP Server");
	tcpServer.begin();
}



//---------------------------------------------------------------------
void WiFi_Start_STA() {
	unsigned long timeout;

	WiFi.mode(WIFI_STA);   //  Workstation

	timeout = millis() + 12000L;
	while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
		delay(10);
	}

	if (WiFi.status() == WL_CONNECTED) {
		server.begin();
		my_WiFi_Mode = WIFI_STA;

#ifdef BGTDEBUG
		Serial.print("Connected IP - Address : ");
		for (int i = 0; i < 3; i++) {
			Serial.print(WiFi.localIP()[i]);
			Serial.print(".");
		}
		Serial.println(WiFi.localIP()[3]);
#endif


	}
	else {
		WiFi.mode(WIFI_OFF);
#ifdef BGTDEBUG
		Serial.println("WLAN-Connection failed");
#endif

	}
}

/*
void WiFi_Start_AP() {
WiFi.mode(WIFI_AP);   // Accesspoint
WiFi.softAP(ssid_ap, password_ap);
server.begin();
IPAddress myIP = WiFi.softAPIP();
my_WiFi_Mode = WIFI_AP;

#ifdef BGTDEBUG
Serial.print("Accesspoint started - Name : ");
Serial.print(ssid_ap);
Serial.print( " IP address: ");
Serial.println(myIP);
#endif
}
*/

void initFilerSystem()
{

	delay(1000);
	Serial.println("Mounting FS...");

	if (!SPIFFS.begin()) {
		Serial.println("Failed to mount file system");
		return;
	}
}

bool saveSettings() {
	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	json["serverName"] = "api.example.com";
	json["accessToken"] = "128du9as8du12eoue8da98h123ueh9h98";

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile) {
		Serial.println("Failed to open config file for writing");
		return false;
	}

	json.printTo(configFile);
	return true;
}

void tcpTraffic(){

	char inString[5000];
	inString[0] = 0;

	uint8_t connected =  m_tcpClient.connected();

	//Serial.print("tcp connection status: ");
	//Serial.println(connected);

	//check if a connection is already opened
	if (!connected)  {
		
		//if no connection is opend, check if there's a new connection
		m_tcpClient = tcpServer.available();

		if (!m_tcpClient)  {
			return;
		}
	}		
	
	Serial.println("tcp Client connected");
			
	int inPtr = 0;
	char readChar = 0;
	//String readData = "";
	while (m_tcpClient.available() && readChar != '\r') {
		readChar = m_tcpClient.read();

		//readData.concat(readChar);
		inString[inPtr++] = readChar;		
	}
	m_tcpClient.flush();
	inString[inPtr] = 0;


	parseTcpData(inString);		
}

void parseTcpData(char *tcpData)
{
	Serial.print("incomming tcp data:");
	Serial.println(tcpData);

	char* command = strtok(tcpData, "?");

	Serial.print("command:");
	Serial.println(command);

	if (strcmp(command, "color") == 0)
	{
		// Find the next command in input string
		
		Serial.print("get the colors from the message");
		
		char* red = strtok(0, "&");
		char* green = strtok(0, "&");
		char* blue = strtok(0, "&");
		
		Serial.print("set colors (R,G,B): ");
		Serial.print(red);
		Serial.print(green);
		Serial.println(blue);
		
	}

		
	


}

//---------------------------------------------------------------------
void WebsiteTraffic() {

	char my_char;
	int htmlPtr = 0;
	int myIdx;
	int myIndex;
	unsigned long my_timeout;

	// Check if a client has connected
	client = server.available();
	if (!client)  {
		return;
	}

	Serial.println("Wifi traffic. Client aviable");

	my_timeout = millis() + 250L;
	while (!client.available() && (millis() < my_timeout)) delay(10);
	delay(10);
	if (millis() > my_timeout)  {
		return;
	}
	//---------------------------------------------------------------------
	htmlPtr = 0;
	my_char = 0;
	while (client.available() && my_char != '\r') {
		my_char = client.read();
		HTML_String[htmlPtr++] = my_char;
	}
	client.flush();
	HTML_String[htmlPtr] = 0;
#ifdef BGTDEBUG
	exhibit("Request : ", HTML_String);
#endif

	Aufruf_Zaehler++;



	if (Find_Start("/?", HTML_String) < 0 && Find_Start("GET / HTTP", HTML_String) < 0) {
		send_not_found();
		return;
	}

	//---------------------------------------------------------------------
	// Benutzereingaben einlesen und verarbeiten
	//---------------------------------------------------------------------
	action = Pick_Parameter_Zahl("ACTION=", HTML_String);

	/*
	// Vor und Nachname
	if ( action == ACTION_SET_NAME) {

	myIndex = Find_End("VORNAME=", HTML_String);
	if (myIndex >= 0) {
	Pick_Text(Vorname, &HTML_String[myIndex], 20);
	#ifdef BGTDEBUG
	exhibit ("Vorname  : ", Vorname);
	#endif
	}

	myIndex = Find_End("NACHNAME=", HTML_String);
	if (myIndex >= 0) {
	Pick_Text(Nachname, &HTML_String[myIndex], 20);
	#ifdef BGTDEBUG
	exhibit ("Nachname  : ", Nachname);
	#endif
	}
	}
	*/
	// Uhrzeit und Datum
	if (action == ACTION_SET_DATE_TIME)
	{
		m_currentState = state_wakupAlarm;
		// UHRZEIT=12%3A35%3A25
		myIndex = Find_End("UHRZEIT=", HTML_String);
		if (myIndex >= 0) {
			Pick_Text(tmp_string, &HTML_String[myIndex], 8);
			Uhrzeit_HH = Pick_N_Zahl(tmp_string, ':', 1);
			Uhrzeit_MM = Pick_N_Zahl(tmp_string, ':', 2);
			Uhrzeit_SS = Pick_N_Zahl(tmp_string, ':', 3);
#ifdef BGTDEBUG
			Serial.print("new alarm time received ");
			Serial.print(Uhrzeit_HH);
			Serial.print(":");
			Serial.print(Uhrzeit_MM);
			Serial.print(":");
			Serial.println(Uhrzeit_SS);
#endif

		}

		myIndex = Find_End("DIMDURATION=", HTML_String);
		if (myIndex >= 0) {
			Pick_Text(tmp_string, &HTML_String[myIndex], 8);

			dimDuration = String(tmp_string).toInt();

			Serial.print("new dim duration received ");
			Serial.println(String(tmp_string).toInt());

		}

		alarmIsSet = true;

		setAlarm(1, Uhrzeit_HH, Uhrzeit_MM, 0);

		colorWipe(strip.Color(0, 0, 0), 5000); // Red

		//generate a good stepSize

		if (m_dimSteps < dimDuration)
		{
			m_dimSteps = dimDuration;
		}


		dimIntervall = dimDuration / m_dimSteps;

		Serial.println("dimIntervall=dimDuration / m_dimSteps");
		Serial.println(dimIntervall);
		Serial.println(" = ");
		Serial.println(dimDuration);
		Serial.println(" / ");
		Serial.println(m_dimSteps);



		/*
		// DATUM=2015-12-31
		myIndex = Find_End("DATUM=", HTML_String);
		if (myIndex >= 0) {
		Pick_Text(tmp_string, &HTML_String[myIndex], 10);
		Datum_JJJJ = Pick_N_Zahl(tmp_string, '-', 1);
		Datum_MM = Pick_N_Zahl(tmp_string, '-', 2);
		Datum_TT = Pick_N_Zahl(tmp_string, '-', 3);
		#ifdef BGTDEBUG
		Serial.print("Neues Datum ");
		Serial.print(Datum_TT);
		Serial.print(".");
		Serial.print(Datum_MM);
		Serial.print(".");
		Serial.println(Datum_JJJJ);
		#endif
		}
		*/
	}



	if (action == ACTION_DEAKTIVATE_ALARM)
	{
		alarmIsSet = false;

		Serial.println("alarm deactivated");

	}


	/*
	if ( action == ACTION_LIES_AUSWAHL) {
	Wochentage = 0;
	for (int i = 0; i < 7; i++) {
	strcpy( tmp_string, "WOCHENTAG");
	strcati( tmp_string, i);
	strcat( tmp_string, "=");
	if (Pick_Parameter_Zahl(tmp_string, HTML_String) == 1)Wochentage |= 1 << i;
	}
	Jahreszeit = Pick_Parameter_Zahl("JAHRESZEIT=", HTML_String);
	Wetter = Pick_Parameter_Zahl("WETTER=", HTML_String);

	}
	*/

	if (action == ACTION_SET_LIGHT)
	{
		m_currentState = state_staticLight;

		colorRed = Pick_Parameter_Zahl("RED=", HTML_String);
		colorGreen = Pick_Parameter_Zahl("GREEN=", HTML_String);
		colorBlue = Pick_Parameter_Zahl("BLUE=", HTML_String);

		Serial.print("red=");
		Serial.println(colorRed);

		Serial.print("green=");
		Serial.println(colorGreen);

		Serial.print("blue=");
		Serial.println(colorBlue);

		colorWipe(strip.Color(colorRed, colorBlue, colorGreen), 50);

		alarmColorRed = colorRed;
		alarmColorGreen = colorGreen;
		alarmColorBlue = colorBlue;

	}

	if (action == ACTION_SET_LIGHTCHANGE)
	{

		m_currentState = state_lightEffect;

		m_colorChangeMode = Pick_Parameter_Zahl("lightChangeMode=", HTML_String);

		Serial.print("lightChangeMode received:");
		Serial.println(m_colorChangeMode);

		m_lightChangeDuration = Pick_Parameter_Zahl("LIGHTCHANGEDURATION=", HTML_String);

		Serial.print("with lightChangeDuration:");
		Serial.println(m_lightChangeDuration);


		

	}

	//---------------------------------------------------------------------
	//Antwortseite aufbauen

	make_HTML01();

	//---------------------------------------------------------------------
	// Header aufbauen
	strcpy(HTTP_Header, "HTTP/1.1 200 OK\r\n");
	strcat(HTTP_Header, "Content-Length: ");
	strcati(HTTP_Header, strlen(HTML_String));
	strcat(HTTP_Header, "\r\n");
	strcat(HTTP_Header, "Content-Type: text/html\r\n");
	strcat(HTTP_Header, "Connection: close\r\n");
	strcat(HTTP_Header, "\r\n");

	/*
  #ifdef BGTDEBUG
  exhibit("Header : ", HTTP_Header);
  exhibit("Laenge Header : ", strlen(HTTP_Header));
  exhibit("Laenge HTML   : ", strlen(HTML_String));
  #endif
  */
	client.print(HTTP_Header);
	delay(20);

	send_HTML();

}

//---------------------------------------------------------------------
// HTML Seite 01 aufbauen
//---------------------------------------------------------------------
void make_HTML01() {

	strcpy(HTML_String, "<!DOCTYPE html>");
	strcat(HTML_String, "<html>");
	strcat(HTML_String, "<head>");
	strcat(HTML_String, "<title>Light control</title>");
	strcat(HTML_String, "</head>");
	strcat(HTML_String, "<body bgcolor=\"#adcede\">");
	strcat(HTML_String, "<font color=\"#000000\" face=\"VERDANA,ARIAL,HELVETICA\">");
	strcat(HTML_String, "<h1>Wakup Light</h1>");

	/*
	//-----------------------------------------------------------------------------------------
	// Textfelder Vor- und Nachname
	strcat( HTML_String, "<h2>Textfelder</h2>");
	strcat( HTML_String, "<form>");
	strcat( HTML_String, "<table>");
	set_colgroup(150, 270, 150, 0, 0);

	strcat( HTML_String, "<tr>");
	strcat( HTML_String, "<td><b>Vorname</b></td>");
	strcat( HTML_String, "<td>");
	strcat( HTML_String, "<input type=\"text\" style= \"width:200px\" name=\"VORNAME\" maxlength=\"20\" Value =\"");
	strcat( HTML_String, Vorname);
	strcat( HTML_String, "\"></td>");
	strcat( HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_SET_NAME);
	strcat( HTML_String, "\">&Uuml;bernehmen</button></td>");
	strcat( HTML_String, "</tr>");

	strcat( HTML_String, "<tr>");
	strcat( HTML_String, "<td><b>Nachname</b></td>");
	strcat( HTML_String, "<td>");
	strcat( HTML_String, "<input type=\"text\" style= \"width:200px\" name=\"NACHNAME\" maxlength=\"20\" Value =\"");
	strcat( HTML_String, Nachname);
	strcat( HTML_String, "\"></td>");
	strcat( HTML_String, "</tr>");

	strcat( HTML_String, "</table>");
	strcat( HTML_String, "</form>");
	strcat( HTML_String, "<br>");

	*/

	//-----------------------------------------------------------------------------------------
	// Uhrzeit + Datum
	strcat(HTML_String, "<h2>Weckzeit</h2>");
	strcat(HTML_String, "<form>");
	strcat(HTML_String, "<table>");
	set_colgroup(150, 270, 150, 0, 0);

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><b>Uhrzeit</b></td>");
	strcat(HTML_String, "<td><input type=\"time\"   style= \"width:100px\" name=\"UHRZEIT\" value=\"");
	strcati2(HTML_String, Uhrzeit_HH);
	strcat(HTML_String, ":");
	strcati2(HTML_String, Uhrzeit_MM);
	//strcat( HTML_String, ":");
	// strcati2( HTML_String, Uhrzeit_SS);
	Serial.print("html site with hour=");
	Serial.println(Uhrzeit_HH);

	strcat(HTML_String, "\"></td>");

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><b>DimDauer</b></td>");
	strcat(HTML_String, "<td><input type=\"number\"   style= \"width:100px\" name=\"DIMDURATION\" value=\"");
	strcati2(HTML_String, dimDuration);
	strcat(HTML_String, "\"></td>");
	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_SET_DATE_TIME);
	strcat(HTML_String, "\">Alarm setzen</button></td>");
	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_DEAKTIVATE_ALARM);
	strcat(HTML_String, "\">Alarm deaktivieren</button></td>");
	strcat(HTML_String, "</tr>");


	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td>");

	if (alarmIsSet == true)
	{
		strcat(HTML_String, "Alarm gesetzt um:  ");
		strcati2(HTML_String, Uhrzeit_HH);
		strcat(HTML_String, ":");
		strcati2(HTML_String, Uhrzeit_MM);

	}
	else
	{
		strcat(HTML_String, "Alarm nicht gesetzt");
	}

	strcat(HTML_String, "</td>");
	strcat(HTML_String, "</tr>");
	/*
	strcat( HTML_String, "<tr>");
	strcat( HTML_String, "<td><b>Datum</b></td>");
	strcat( HTML_String, "<td><input type=\"date\"  style= \"width:100px\" name=\"DATUM\" value=\"");
	strcati( HTML_String, Datum_JJJJ);
	strcat( HTML_String, "-");
	strcati2( HTML_String, Datum_MM);
	strcat( HTML_String, "-");
	strcati2( HTML_String, Datum_TT);
	strcat( HTML_String, "\"></td></tr>");
	*/
	strcat(HTML_String, "</table>");
	strcat(HTML_String, "</form>");
	strcat(HTML_String, "<br>");
	/*
	//-----------------------------------------------------------------------------------------
	// Checkboxen
	strcat( HTML_String, "<h2>Checkbox, Radiobutton und Combobox</h2>");
	strcat( HTML_String, "<form>");
	strcat( HTML_String, "<table>");
	set_colgroup(150, 270, 150, 0, 0);

	strcat( HTML_String, "<tr>");
	strcat( HTML_String, "<td><b>Wochentage</b></td>");

	strcat( HTML_String, "<td>");
	for (int i = 0; i < 7; i++) {
	if (i == 5)strcat( HTML_String, "<br>");
	strcat( HTML_String, "<input type=\"checkbox\" name=\"WOCHENTAG");
	strcati( HTML_String, i);
	strcat( HTML_String, "\" id = \"WT");
	strcati( HTML_String, i);
	strcat( HTML_String, "\" value = \"1\" ");
	if (Wochentage & 1 << i) strcat( HTML_String, "checked ");
	strcat( HTML_String, "> ");
	strcat( HTML_String, "<label for =\"WT");
	strcati( HTML_String, i);
	strcat( HTML_String, "\">");
	strcat( HTML_String, Wochentage_tab[i]);
	strcat( HTML_String, "</label>");
	}
	strcat( HTML_String, "</td>");

	strcat( HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_LIES_AUSWAHL);
	strcat( HTML_String, "\">&Uuml;bernehmen</button></td>");
	strcat( HTML_String, "</tr>");

	//-----------------------------------------------------------------------------------------
	// Radiobuttons

	for (int i = 0; i < 4; i++) {
	strcat( HTML_String, "<tr>");
	if (i == 0)  strcat( HTML_String, "<td><b>Jahreszeit</b></td>");
	else strcat( HTML_String, "<td> </td>");
	strcat( HTML_String, "<td><input type = \"radio\" name=\"JAHRESZEIT\" id=\"JZ");
	strcati( HTML_String, i);
	strcat( HTML_String, "\" value=\"");
	strcati( HTML_String, i);
	strcat( HTML_String, "\"");
	if (Jahreszeit == i)strcat( HTML_String, " CHECKED");
	strcat( HTML_String, "><label for=\"JZ");
	strcati( HTML_String, i);
	strcat( HTML_String, "\">");
	strcat( HTML_String, Jahreszeiten_tab[i]);
	strcat( HTML_String, "</label></td></tr>");
	}

	//-----------------------------------------------------------------------------------------
	// Combobox
	strcat( HTML_String, "<tr><td><b>Wetter</b></td>");

	strcat( HTML_String, "<td>");
	strcat( HTML_String, "<select name = \"WETTER\" style= \"width:160px\">");
	for (int i = 0; i < 4; i++) {
	strcat( HTML_String, "<option ");
	if (Wetter == i)strcat( HTML_String, "selected ");
	strcat( HTML_String, "value = \"");
	strcati( HTML_String, i);
	strcat(HTML_String, "\">");
	strcat(HTML_String, Wetter_tab[i]);
	strcat(HTML_String, "</option>");
	}
	strcat( HTML_String, "</select>");
	strcat( HTML_String, "</td></tr>");

	strcat( HTML_String, "</table>");
	strcat( HTML_String, "</form>");
	strcat( HTML_String, "<br>");
	*/
	//-----------------------------------------------------------------------------------------
	// Slider
	strcat(HTML_String, "<h2>Farbeinstellung</h2>");
	strcat(HTML_String, "<form>");
	strcat(HTML_String, "<table>");
	//set_colgroup(150, 270, 150, 0, 0);

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><b>red</b></td>");

	strcat(HTML_String, "<td>");
	strcat(HTML_String, "<input type=\"range\" name=\"RED\" min=\"0\" max=\"255\" value = \"");
	strcati(HTML_String, colorRed);
	strcat(HTML_String, "\">");
	strcat(HTML_String, "</td>");

	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "<tr>");
	// strcat(HTML_String, "<br>");

	strcat(HTML_String, "<td><b>green</b></td>");

	strcat(HTML_String, "<td>");
	strcat(HTML_String, "<input type=\"range\" name=\"GREEN\" min=\"0\" max=\"255\" value = \"");
	strcati(HTML_String, colorGreen);
	strcat(HTML_String, "\">");
	strcat(HTML_String, "</td>");

	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "<tr>");
	//strcat(HTML_String, "<br>");
	strcat(HTML_String, "<td><b>blue</b></td>");

	strcat(HTML_String, "<td>");
	strcat(HTML_String, "<input type=\"range\" name=\"BLUE\" min=\"0\" max=\"255\" value = \"");
	strcati(HTML_String, colorBlue);
	strcat(HTML_String, "\">");
	strcat(HTML_String, "</td>");

	strcat(HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_SET_LIGHT);
	strcat(HTML_String, "\">&Uuml;bernehmen</button></td>");
	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "</table>");
	strcat(HTML_String, "</form>");
	strcat(HTML_String, "<BR>");


	strcat(HTML_String, "<h2>Farbwechsel</h2>");
	strcat(HTML_String, "<form>");
	strcat(HTML_String, "<table>");

	strcat(HTML_String, "<tr>");
	strcat(HTML_String, "<td><b>Wechsel Dauer </b></td>");
	strcat(HTML_String, "<td><input type=\"number\"   style= \"width:100px\" name=\"LIGHTCHANGEDURATION\" value=\"");
	strcati2(HTML_String, m_lightChangeDuration);
	strcat(HTML_String, "\"></td>");
	strcat(HTML_String, "</tr>");

	strcat(HTML_String, "<select name = \"lightChangeMode\" style= \"width:160px\">");

	strcat(HTML_String, "<option value=\"1\">rainbow</option>");
	strcat(HTML_String, "<option value=\"2\">rainbow cycle</option>");

	strcat(HTML_String, "<td><button style= \"width:100px\" name=\"ACTION\" value=\"");
	strcati(HTML_String, ACTION_SET_LIGHTCHANGE);
	strcat(HTML_String, "\">&Uuml;bernehmen</button></td>");

	strcat(HTML_String, "</select>");
	strcat(HTML_String, "</td></tr>");

	strcat(HTML_String, "</table>");
	strcat(HTML_String, "</form>");
	strcat(HTML_String, "<BR>");


	strcat(HTML_String, "<BR>");

	strcat(HTML_String, "<FONT SIZE=-1>");
	strcat(HTML_String, "Aufrufz&auml;hler : ");
	strcati(HTML_String, Aufruf_Zaehler);
	strcat(HTML_String, "<BR>");

	RtcDateTime currentTime = rtc.GetDateTime();    //get the time from the RTC

	strcat(HTML_String, "Eingestellte Uhrzeit bei Seitenaufruf : ");
	strcati(HTML_String, currentTime.Hour());
	strcat(HTML_String, ":");
	strcati(HTML_String, currentTime.Minute());

	strcat(HTML_String, "</font>");
	strcat(HTML_String, "</font>");
	strcat(HTML_String, "</body>");
	strcat(HTML_String, "</html>");
}

//--------------------------------------------------------------------------
void send_not_found() {
#ifdef BGTDEBUG
	Serial.println("Sende Not Found");
#endif
	client.print("HTTP/1.1 404 Not Found\r\n\r\n");
	delay(20);
	client.stop();
}

//--------------------------------------------------------------------------
void send_HTML() {
	char my_char;
	int  my_len = strlen(HTML_String);
	int  my_ptr = 0;
	int  my_send = 0;

	//--------------------------------------------------------------------------
	// in Portionen senden
	while ((my_len - my_send) > 0) {
		my_send = my_ptr + MAX_PACKAGE_SIZE;
		if (my_send > my_len) {
			client.print(&HTML_String[my_ptr]);
			delay(20);
#ifdef BGTDEBUG
			// Serial.println(&HTML_String[my_ptr]);
#endif
			my_send = my_len;
		}
		else {
			my_char = HTML_String[my_send];
			// Auf Anfang eines Tags positionieren
			while (my_char != '<') my_char = HTML_String[--my_send];
			HTML_String[my_send] = 0;
			client.print(&HTML_String[my_ptr]);
			delay(20);
#ifdef BGTDEBUG
			// Serial.println(&HTML_String[my_ptr]);
#endif
			HTML_String[my_send] = my_char;
			my_ptr = my_send;
		}
	}
	client.stop();
}

//----------------------------------------------------------------------------------------------
void set_colgroup(int w1, int w2, int w3, int w4, int w5) {
	strcat(HTML_String, "<colgroup>");
	set_colgroup1(w1);
	set_colgroup1(w2);
	set_colgroup1(w3);
	set_colgroup1(w4);
	set_colgroup1(w5);
	strcat(HTML_String, "</colgroup>");

}
//------------------------------------------------------------------------------------------
void set_colgroup1(int ww) {
	if (ww == 0) return;
	strcat(HTML_String, "<col width=\"");
	strcati(HTML_String, ww);
	strcat(HTML_String, "\">");
}


//---------------------------------------------------------------------
void strcati(char* tx, int i) {
	char tmp[8];

	itoa(i, tmp, 10);
	strcat(tx, tmp);
}

//---------------------------------------------------------------------
void strcati2(char* tx, int i) {
	char tmp[8];

	itoa(i, tmp, 10);
	if (strlen(tmp) < 2) strcat(tx, "0");
	strcat(tx, tmp);
}

//---------------------------------------------------------------------
int Pick_Parameter_Zahl(const char * par, char * str) {
	int myIdx = Find_End(par, str);

	if (myIdx >= 0) return  Pick_Dec(str, myIdx);
	else return -1;
}
//---------------------------------------------------------------------
int Find_End(const char * such, const char * str) {
	int tmp = Find_Start(such, str);
	if (tmp >= 0)tmp += strlen(such);
	return tmp;
}

//---------------------------------------------------------------------
int Find_Start(const char * such, const char * str) {
	int tmp = -1;
	int ww = strlen(str) - strlen(such);
	int ll = strlen(such);

	for (int i = 0; i <= ww && tmp == -1; i++) {
		if (strncmp(such, &str[i], ll) == 0) tmp = i;
	}
	return tmp;
}
//---------------------------------------------------------------------
int Pick_Dec(const char * tx, int idx) {
	int tmp = 0;

	for (int p = idx; p < idx + 5 && (tx[p] >= '0' && tx[p] <= '9'); p++) {
		tmp = 10 * tmp + tx[p] - '0';
	}
	return tmp;
}
//----------------------------------------------------------------------------
int Pick_N_Zahl(const char * tx, char separator, byte n) {

	int ll = strlen(tx);
	int tmp = -1;
	byte anz = 1;
	byte i = 0;
	while (i < ll && anz < n) {
		if (tx[i] == separator)anz++;
		i++;
	}
	if (i < ll) return Pick_Dec(tx, i);
	else return -1;
}

//---------------------------------------------------------------------
int Pick_Hex(const char * tx, int idx) {
	int tmp = 0;

	for (int p = idx; p < idx + 5 && ((tx[p] >= '0' && tx[p] <= '9') || (tx[p] >= 'A' && tx[p] <= 'F')); p++) {
		if (tx[p] <= '9')tmp = 16 * tmp + tx[p] - '0';
		else tmp = 16 * tmp + tx[p] - 55;
	}

	return tmp;
}

//---------------------------------------------------------------------
void Pick_Text(char * tx_ziel, char  * tx_quelle, int max_ziel) {

	int p_ziel = 0;
	int p_quelle = 0;
	int len_quelle = strlen(tx_quelle);

	while (p_ziel < max_ziel && p_quelle < len_quelle && tx_quelle[p_quelle] && tx_quelle[p_quelle] != ' ' && tx_quelle[p_quelle] != '&') {
		if (tx_quelle[p_quelle] == '%') {
			tx_ziel[p_ziel] = (HexChar_to_NumChar(tx_quelle[p_quelle + 1]) << 4) + HexChar_to_NumChar(tx_quelle[p_quelle + 2]);
			p_quelle += 2;
		}
		else if (tx_quelle[p_quelle] == '+') {
			tx_ziel[p_ziel] = ' ';
		}
		else {
			tx_ziel[p_ziel] = tx_quelle[p_quelle];
		}
		p_ziel++;
		p_quelle++;
	}

	tx_ziel[p_ziel] = 0;
}
//---------------------------------------------------------------------
char HexChar_to_NumChar(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 55;
	return 0;
}

#ifdef BGTDEBUG
//---------------------------------------------------------------------
void exhibit(const char * tx, int v) {
	Serial.print(tx);
	Serial.println(v);
}
//---------------------------------------------------------------------
void exhibit(const char * tx, unsigned int v) {
	Serial.print(tx);
	Serial.println(v);
}
//---------------------------------------------------------------------
void exhibit(const char * tx, unsigned long v) {
	Serial.print(tx);
	Serial.println(v);
}
//---------------------------------------------------------------------
void exhibit(const char * tx, const char * v) {
	Serial.print(tx);
	Serial.println(v);
}
#endif

//LED-stuff

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
	for (uint16_t i = 0; i < strip.numPixels(); i++) {
		strip.setPixelColor(i, c);
		strip.show();
		delay(wait);
	}
}


/*-------- NTP code ----------*/

void setupNTPSync()
{
	Serial.println("Starting NTP-UDP");
	timeClient.begin();
}


/*RTC code*/

int setRtcTimeFromNTP()
{
	Serial.println("set rtc time from ntp:");

	timeClient.update();

	Serial.println(timeClient.getEpochTime());

	time_t currentTimeEpoche = timeClient.getEpochTime();

	int currentYear = year(currentTimeEpoche);
	//if read time is too smal, a failure occured
	if (currentYear < 2010)
	{
		//could't get the time
		Serial.println("FAILED to set time from NTP  !!!");
		return -1;
	}
	else
	{

		bool useSummerTime = summertime(year(currentTimeEpoche), month(currentTimeEpoche), day(currentTimeEpoche), hour(currentTimeEpoche), 0);

		int summerTimeAdd;

		if (useSummerTime == true)
		{
			Serial.println("use summertime -> + 1h");
			summerTimeAdd = 1;
		}
		else
		{
			Serial.println("don't use summertime");
			summerTimeAdd = 0;
		}

		RtcDateTime currentTime = RtcDateTime(year(currentTimeEpoche), month(currentTimeEpoche), day(currentTimeEpoche), hour(currentTimeEpoche) + summerTimeAdd, minute(currentTimeEpoche), second(currentTimeEpoche));  //define date and time object
		rtc.SetDateTime(currentTime);                                  //configure the RTC with object

		//serial time output
		char str[15];   //declare a string as an array of chars  

		sprintf(str, "%d / %d / %d %d:%d : %d",     //%d allows to print an integer to the string
			currentTime.Year(),                      //get year method
			currentTime.Month(),                 //get month method
			currentTime.Day(),                      //get day method
			currentTime.Hour(),                   //get hour method
			currentTime.Minute(),              //get minute method
			currentTime.Second()               //get second method
			);

		Serial.println(str);     //print the string to the serial port

	}

	return 0;
}

time_t getRtcTime()
{
	Serial.println("get the time from rtc");

	if (!rtc.IsDateTimeValid())
	{
		Serial.println("rtc time is not valid!");

	}


	RtcDateTime currentTime = rtc.GetDateTime();    //get the time from the RTC

	uint32_t currentTimeSinceEpoch = currentTime.Epoch64Time();

	Serial.print("\nSeconds since 1970 from rtc:");
	Serial.println(currentTimeSinceEpoch);

	//serial time output
	char str[15];   //declare a string as an array of chars  

	sprintf(str, "%d / %d / %d %d:%d : %d",     //%d allows to print an integer to the string
		currentTime.Year(),                      //get year method
		currentTime.Month(),                 //get month method
		currentTime.Day(),                      //get day method
		currentTime.Hour(),                   //get hour method
		currentTime.Minute(),              //get minute method
		currentTime.Second()               //get second method
		);
	Serial.println("\nformatted time: ");
	Serial.print(str);     //print the string to the serial port
}

void setAlarm(uint8_t dayOf, uint8_t hour, uint8_t minute, uint8_t second)
{
	Serial.println("set Alarm1");
	//uint8_t dayOf, uint8_t hour, uint8_t minute, uint8_t second

	DS3231AlarmOne alarm1(
		dayOf,
		hour,
		minute,
		second,
		DS3231AlarmOneControl_SecondsMatch);

	//DS3231AlarmOneControl_HoursMinutesSecondsMatch
	/*
	DS3231AlarmOne alarm1(
	0,
	0,
	0,
	10,
	DS3231AlarmOneControl_SecondsMatch);
	*/
	rtc.SetAlarmOne(alarm1);
	rtc.LatchAlarmsTriggeredFlags();

}


time_t getAlarmEpochSeconds()
{
	DS3231AlarmOne alarm1 = rtc.GetAlarmOne();

	RtcDateTime currentTime = rtc.GetDateTime();    //get the time from the RTC

	//for the alarm we don't use a different year or month
	time_t alarmTime = tmConvert_t((int)currentTime.Year(), currentTime.Month(), alarm1.DayOf(), alarm1.Minute(), alarm1.Minute(), alarm1.Second());

	return alarmTime;
}


void getAlarmTime(uint8_t *hour, uint8_t *minute)
{
	DS3231AlarmOne alarm1 = rtc.GetAlarmOne();

	Serial.print("get alarm1: ");
	Serial.print(alarm1.Hour());
	Serial.print(":");
	Serial.println(alarm1.Minute());

	*hour = alarm1.Hour();
	*minute = alarm1.Minute();
}


time_t tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss)
{
	tmElements_t tmSet;
	tmSet.Year = YYYY - 1970;
	tmSet.Month = MM;
	tmSet.Day = DD;
	tmSet.Hour = hh;
	tmSet.Minute = mm;
	tmSet.Second = ss;
	return makeTime(tmSet);
}


boolean summertime(int year, int month, int day, int hour, byte tzHours)
// European Daylight Savings Time calculation by "jurs" for German Arduino Forum
// input parameters: "normal time" for year, month, day, hour and tzHours (0=UTC, 1=MEZ)
// return value: returns true during Daylight Saving Time, false otherwise
{
	static int x1, x2, lastyear; // Zur Beschleunigung des Codes ein Cache für einige statische Variablen
	static byte lasttzHours;
	int x3;
	if (month<3 || month>10) return false; // keine Sommerzeit in Jan, Feb, Nov, Dez
	if (month > 3 && month < 10) return true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
	// der nachfolgende Code wird nur für Monat 3 und 10 ausgeführt
	// Umstellung erfolgt auf Stunde utc_hour=1, in der Zeitzone Berlin entsprechend 2 Uhr MEZ
	// Es wird ein Cache-Speicher für die Variablen x1 und x2 verwendet, 
	// dies beschleunigt die Berechnung, wenn sich das Jahr bei Folgeaufrufen nicht ändert
	// x1 und x2 werden nur neu Berechnet, wenn sich das Jahr bei nachfolgenden Aufrufen ändert
	if (year != lastyear || tzHours != lasttzHours)
	{ // Umstellungsbeginn und -ende
		x1 = 1 + tzHours + 24 * (31 - (5 * year / 4 + 4) % 7);
		x2 = 1 + tzHours + 24 * (31 - (5 * year / 4 + 1) % 7);
		lastyear = year;
		lasttzHours = tzHours;
	}
	x3 = hour + 24 * day;
	if (month == 3 && x3 >= x1 || month == 10 && x3 < x2) return true; else return false;
}

//LED-Stripe functions

void rainbow(unsigned long wait) {
	uint16_t i;

	unsigned long currentTime = millis();


	if (currentTime > m_lastEffectTime + wait)
	{
		Serial.println("wait time for effect rainbow is over");
		Serial.print("last effectTime:");
		Serial.println(m_lastEffectTime);
		Serial.print("wait time:");
		Serial.println(wait);
		Serial.print("currentTime:");
		Serial.println(currentTime);


		m_lastEffectTime = currentTime;

		//reset the cycle counter and start with zero
		if (m_lastEffectStep > 255)
		{
			m_lastEffectStep = 0;
		}

		for (i = 0; i < strip.numPixels(); i++) {
			strip.setPixelColor(i, Wheel((i + m_lastEffectStep) & 255));
		}
		strip.show();

		m_lastEffectStep = m_lastEffectStep + 2;
	}
}

void rainbowCycle(unsigned long wait) {
	uint16_t i;
	
	unsigned long currentTime = millis();
	
	if (currentTime > m_lastEffectTime + wait)
	{
		Serial.println("wait time for effect rainbowCycle is over");
		Serial.print("last effectStep:");
		Serial.println(m_lastEffectStep);

		m_lastEffectTime = currentTime;

		//reset the cycle counter and start with zero
		if (m_lastEffectStep > 255)
		{
			m_lastEffectStep = 0;
		}

		for (i = 0; i < strip.numPixels(); i++)
		{
			strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + m_lastEffectStep) & 255));
		}
		strip.show();
		m_lastEffectStep = m_lastEffectStep + 2;
	}
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
	WheelPos = 255 - WheelPos;
	if (WheelPos < 85) {
		return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
	}
	if (WheelPos < 170) {
		WheelPos -= 85;
		return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
	}
	WheelPos -= 170;
	return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
