/*
   ESP8266 + FastLED + IR Remote: https://github.com/jasoncoon/esp8266-fastled-webserver
   Copyright (C) 2015-2017 Jason Coon

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// #define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#include <FastLED.h>
FASTLED_USING_NAMESPACE

extern "C" {
#include "user_interface.h"
}

#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>

#include "GradientPalettes.h"

#define MQTT_MAX_PACKET_SIZE 256
#include <PubSubClient.h>

#define ARRAY_SIZE( A ) ( sizeof( A ) / sizeof( ( A )[0] ) )

#include "Field.h"

#define VERSION "0.1"

#define HOSTNAME "Psilocybe-" ///< Hostname. The setup function adds the Chip ID at the end.

WiFiClient espClient;
PubSubClient mqtt_client( espClient );

// AP mode password
const char WiFiAPPSK[] = "";

int OTA_in_progress = 0;

ESP8266WebServer webServer( 80 );
WebSocketsServer webSocketsServer = WebSocketsServer( 81 );
ESP8266HTTPUpdateServer httpUpdateServer;

// does not build with eps8266 SDK > 3 see https://github.com/arrowhead-f/ArrowheadESP/issues/6
// use 2.7.4 instead

#include "FSBrowser.h"

#define DATA_PIN D4 // was D5
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 241 // 73 was 241

#define MILLI_AMPS 4000 // IMPORTANT: set the max milli-Amps of your power supply (4A = 4000mA)

// unused
// #define FRAMES_PER_SECOND \ 	120 // here you can control the speed. With the Access Point / Web Server the animations
// run a bit slower.

CRGB leds[NUM_LEDS];

const uint8_t brightnessCount = 5;
uint8_t brightnessMap[brightnessCount] = { 16, 32, 64, 128, 255 };
uint8_t brightnessIndex = 0;

// ten seconds per color palette makes a good demo
// 20-120 is better for deployment
uint8_t secondsPerPalette = 10;

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
uint8_t cooling = 49;

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
uint8_t sparking = 60;

uint8_t speed = 30;

///////////////////////////////////////////////////////////////////////

// Forward declarations of an array of cpt-city gradient palettes, and
// a count of how many there are.  The actual color palette definitions
// are at the bottom of this file.
extern const TProgmemRGBGradientPalettePtr gGradientPalettes[];

uint8_t gCurrentPaletteNumber = 0;

CRGBPalette16 gCurrentPalette( CRGB::Black );
CRGBPalette16 gTargetPalette( gGradientPalettes[0] );

CRGBPalette16 IceColors_p = CRGBPalette16( CRGB::Black, CRGB::Blue, CRGB::Aqua, CRGB::White );

uint8_t currentPatternIndex = 0; // Index number of which pattern is current
uint8_t autoplay = 0;

String mqttServer = String( "127.0.0.1" );
String mqttPrefix = String( "psilocybe/psilo1/" );
uint8_t mqttEnabled = 0;

uint8_t autoplayDuration = 10;
unsigned long autoPlayTimeout = 0;

uint8_t currentPaletteIndex = 0;

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

CRGB solidColor = CRGB::Blue;

//// A UDP instance to let us send and receive packets over UDP
// WiFiUDP udp;

// unsigned int udpLocalPort = 2390;      // local port to listen for UDP packets

void wifiManagerCallback( WiFiManager* myWiFiManager ) {
	Serial.println( "Entered config mode" );
	Serial.println( WiFi.softAPIP() );
	Serial.println( myWiFiManager->getConfigPortalSSID() );
}

// scale the brightness of all pixels down
void dimAll( byte value ) {
	for( int i = 0; i < NUM_LEDS; i++ ) {
		leds[i].nscale8( value );
	}
}

uint8_t paletteIndex = 0;

typedef struct {
	CRGBPalette16 palette;
	String name;
} PaletteAndName;
typedef PaletteAndName PaletteAndNameList[];

const CRGBPalette16 palettes[] = { RainbowColors_p, RainbowStripeColors_p, CloudColors_p, LavaColors_p,
                                   OceanColors_p,   ForestColors_p,        PartyColors_p, HeatColors_p };

const uint8_t paletteCount = ARRAY_SIZE( palettes );

const String paletteNames[paletteCount] = {
    "Rainbow", "Rainbow Stripe", "Cloud", "Lava", "Ocean", "Forest", "Party", "Heat",
};

CRGBPalette16 currentPalette( CRGB::Black );
CRGBPalette16 targetPalette = palettes[paletteIndex];

typedef void ( *Pattern )();
typedef Pattern PatternList[];
typedef struct {
	Pattern pattern;
	String name;
} PatternAndName;
typedef PatternAndName PatternAndNameList[];

#include "Disk.h"
#include "Map.h"
#include "Noise.h"
#include "PolarNoise.h"
#include "TwinkleFOX.h"
#include "Twinkles.h"
// #include "Clock.h"

// List of patterns to cycle through.  Each is defined as a separate function below.

PatternAndNameList patterns = { { pride, "Pride" },
                                { colorWaves, "Color Waves" },

                                // disk patterns
                                { radialPaletteShift, "Radial Palette Shift" },
                                { radialPaletteShift2, "Radial Palette Shift 2" },
                                { paletteArcs, "Palette Arcs" },
                                { decayingOrbits, "Decaying Orbits" },

                                // { handClock,              "Analog Clock" },
                                // { arcClock,               "Arc Clock" },
                                // { rimClock,               "Rim Clock" },

                                // XY map patterns
                                { wave, "Wave" },
                                { pulse, "Pulse" },
                                { horizontalRainbow, "Horizontal Rainbow" },
                                { verticalRainbow, "Vertical Rainbow" },
                                { diagonalRainbow, "Diagonal Rainbow" },
                                { fire, "Fire" },
                                { water, "Water" },

                                // noise patterns (XY and Polar variations)
                                { fireNoise, "Fire Noise" },
                                { fireNoise2, "Fire Noise 2" },
                                { lavaNoise, "Lava Noise" },
                                { rainbowNoise, "Rainbow Noise" },
                                { rainbowStripeNoise, "Rainbow Stripe Noise" },
                                { partyNoise, "Party Noise" },
                                { forestNoise, "Forest Noise" },
                                { cloudNoise, "Cloud Noise" },
                                { oceanNoise, "Ocean Noise" },
                                { blackAndWhiteNoise, "Black & White Noise" },
                                { blackAndBlueNoise, "Black & Blue Noise" },

                                { gradientPalettePolarNoise, "Gradient Palette Polar Noise" },
                                { palettePolarNoise, "Palette Polar Noise" },

                                { firePolarNoise, "Fire Polar Noise" },
                                { firePolarNoise2, "Fire Polar Noise 2" },
                                { lavaPolarNoise, "Lava Polar Noise" },
                                { rainbowPolarNoise, "Rainbow Polar Noise" },
                                { rainbowStripePolarNoise, "Rainbow Stripe Polar Noise" },
                                { partyPolarNoise, "Party Polar Noise" },
                                { forestPolarNoise, "Forest Polar Noise" },
                                { cloudPolarNoise, "Cloud Polar Noise" },
                                { oceanPolarNoise, "Ocean Polar Noise" },
                                { blackAndWhitePolarNoise, "Black & White Polar Noise" },
                                { blackAndBluePolarNoise, "Black & Blue Polar Noise" },

                                // twinkle patterns
                                { rainbowTwinkles, "Rainbow Twinkles" },
                                { snowTwinkles, "Snow Twinkles" },
                                { cloudTwinkles, "Cloud Twinkles" },
                                { incandescentTwinkles, "Incandescent Twinkles" },

                                // TwinkleFOX patterns
                                { retroC9Twinkles, "Retro C9 Twinkles" },
                                { redWhiteTwinkles, "Red & White Twinkles" },
                                { blueWhiteTwinkles, "Blue & White Twinkles" },
                                { redGreenWhiteTwinkles, "Red, Green & White Twinkles" },
                                { fairyLightTwinkles, "Fairy Light Twinkles" },
                                { snow2Twinkles, "Snow 2 Twinkles" },
                                { hollyTwinkles, "Holly Twinkles" },
                                { iceTwinkles, "Ice Twinkles" },
                                { partyTwinkles, "Party Twinkles" },
                                { forestTwinkles, "Forest Twinkles" },
                                { lavaTwinkles, "Lava Twinkles" },
                                { fireTwinkles, "Fire Twinkles" },
                                { cloud2Twinkles, "Cloud 2 Twinkles" },
                                { oceanTwinkles, "Ocean Twinkles" },

                                { rainbow, "Rainbow" },
                                { rainbowWithGlitter, "Rainbow With Glitter" },
                                { rainbowSolid, "Solid Rainbow" },
                                { confetti, "Confetti" },
                                { sinelon, "Sinelon" },
                                { bpm, "Beat" },
                                { juggle, "Juggle" },
                                { fire, "Fire" },
                                { water, "Water" },

                                { showSolidColor, "Solid Color" } };

const uint8_t patternCount = ARRAY_SIZE( patterns );

#include "Fields.h"

// define a mqtt prefix

int hexDigitToInt( char c ) {
	if( c >= '0' && c <= '9' ) {
		return c - '0';
	} else if( c >= 'A' && c <= 'F' ) {
		return ( c - 'A' ) + 10;
	} else if( c >= 'a' && c <= 'f' ) {
		return ( c - 'a' ) + 10;
	}
}

void mqtt_callback( char* topic, byte* payload, unsigned int length ) {
	Serial.print( "Message arrived [" );
	Serial.print( topic );
	Serial.print( "] " );
	for( int i = 0; i < length; i++ ) {
		Serial.print( (char)payload[i] );
	}
	Serial.println();
	int len = mqttPrefix.length() + 4;
	// psilocybe/psilo1/CMD/ -> len = 21
	if( strlen( topic ) > len ) {
		if( strcmp( topic + len, "power" ) == 0 ) {
			setPower( strncmp( (char*)payload, "on", length ) == 0 ? 1 : 0 );
		}
		if( strcmp( topic + len, "brightness" ) == 0 ) {
			setBrightness( String( (char*)payload ).toInt() );
		}
		if( strcmp( topic + len, "autoplay" ) == 0 ) {
			setAutoplay( String( (char*)payload ).toInt() );
		}
		if( strcmp( topic + len, "pattern" ) == 0 ) {
			setPattern( String( (char*)payload ).toInt() );
		}
		if( strcmp( topic + len, "palette" ) == 0 ) {
			setPalette( String( (char*)payload ).toInt() );
		}
		if( strcmp( topic + len, "autoplayDuration" ) == 0 ) {
			setAutoplayDuration( String( (char*)payload ).toInt() );
		}
		if( strcmp( topic + len, "solidcolor" ) == 0 ) {
			if( payload[0] == '#' && length == 7 ) {
				int r = hexDigitToInt( payload[1] ) * 16 + hexDigitToInt( payload[2] );
				int g = hexDigitToInt( payload[3] ) * 16 + hexDigitToInt( payload[4] );
				int b = hexDigitToInt( payload[5] ) * 16 + hexDigitToInt( payload[6] );
				setSolidColor( CRGB( r, g, b ) );
			}
		}
		if( strcmp( topic + len, "speed" ) == 0 ) {
			setSpeed( String( (char*)payload ).toInt() );
		}
	}
}

char* getTopic( String prefix, const char* postfix ) {
	static char topic[128];
	strcpy( topic, prefix.c_str() );
	strcat( topic, postfix );
	return topic;
}

unsigned long mqtt_reconnect_time = 0;
int mqtt_reconnect_attempts = 0;

void mqtt_reconnect() {
	// Loop until we're reconnected
	if( !mqtt_client.connected() && mqttEnabled && millis() > mqtt_reconnect_time ) {
		mqtt_client.setServer( mqttServer.c_str(), 1883 );
		Serial.print( "Attempting MQTT connection to '" );
		Serial.print( mqttServer );
		Serial.print( "' ... " );
		// Create a random client ID
		String clientId = "ESP8266Client-";
		clientId += String( random( 0xffff ), HEX );
		// Attempt to connect
		if( mqtt_client.connect( clientId.c_str() ) ) {
			Serial.println( "connected" );
			// Once connected, publish an announcement...
			//  use configured prefix
			String json = "{ \"clientId\": \"" + clientId + "\", \"IP\": \"" + WiFi.localIP().toString() +
			              "\",\"version\":\"" + String( VERSION ) + "\" }";
			int l = json.length();
			Serial.println( "MQTT msg: " + json + ", len: " + l );
			mqtt_client.publish( getTopic( mqttPrefix, "info" ), json.c_str() );
			// ... and resubscribe
			// use configured prefix
			char* topic = getTopic( mqttPrefix, "cmd/#" );
			Serial.print( "Subscribing to '" );
			Serial.print( topic );
			Serial.println( "'" );
			mqtt_client.subscribe( topic );
			mqtt_reconnect_attempts = 0;
		} else {
			Serial.print( "failed, rc=" );
			Serial.print( mqtt_client.state() );
			Serial.print( " try again in " );
			int sec = 5 + mqtt_reconnect_attempts * 3;
			Serial.print( sec );
			Serial.println( " seconds" );
			mqtt_reconnect_attempts++;
			// Wait 5 seconds before retrying
			mqtt_reconnect_time = millis() + sec * 1000;
		}
	}
}

unsigned long speedTime1 = 0;
unsigned long speedTime2 = 0;

void setup() {
	WiFi.setSleepMode( WIFI_NONE_SLEEP );

	Serial.begin( 115200 );
	delay( 100 );
	Serial.setDebugOutput( true );

	FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>( leds, NUM_LEDS ); // for WS2812 (Neopixel)
	// FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS); // for APA102 (Dotstar)
	FastLED.setDither( false );
	FastLED.setCorrection( TypicalLEDStrip );
	FastLED.setBrightness( brightness );
	FastLED.setMaxPowerInVoltsAndMilliamps( 5, MILLI_AMPS );
	fill_solid( leds, NUM_LEDS, CRGB::Black );
	FastLED.show();

	wifi_station_set_hostname( "psilocybe" );
	Serial.println( "Initializing WiFi" );
	WiFiManager wifiManager;
	wifiManager.setAPCallback( wifiManagerCallback );
	if( !wifiManager.autoConnect( "psilocybe" ) ) {
		Serial.println( "failed to connect, timeout" );
		delay( 1000 );
		ESP.reset();
	}

	setupRings();

	EEPROM.begin( 512 );
	loadSettings();
	if( mqttEnabled ) {
		mqtt_client.setServer( mqttServer.c_str(), 1883 );
		mqtt_client.setCallback( mqtt_callback );
	}

	FastLED.setBrightness( brightness );

	//  irReceiver.enableIRIn(); // Start the receiver

	Serial.println();
	Serial.print( F( "Heap: " ) );
	Serial.println( system_get_free_heap_size() );
	Serial.print( F( "Boot Vers: " ) );
	Serial.println( system_get_boot_version() );
	Serial.print( F( "CPU: " ) );
	Serial.println( system_get_cpu_freq() );
	Serial.print( F( "SDK: " ) );
	Serial.println( system_get_sdk_version() );
	Serial.print( F( "Chip ID: " ) );
	Serial.println( system_get_chip_id() );
	Serial.print( F( "Flash ID: " ) );
	Serial.println( spi_flash_get_id() );
	Serial.print( F( "Flash Size: " ) );
	Serial.println( ESP.getFlashChipRealSize() );
	Serial.print( F( "Vcc: " ) );
	Serial.println( ESP.getVcc() );
	Serial.print( "IP address: " );
	Serial.println( WiFi.localIP() );
	Serial.println();

	SPIFFS.begin();
	{
		Dir dir = SPIFFS.openDir( "/" );
		while( dir.next() ) {
			String fileName = dir.fileName();
			size_t fileSize = dir.fileSize();
			Serial.printf( "FS File: %s, size: %s\n", fileName.c_str(), String( fileSize ).c_str() );
		}
		Serial.printf( "\n" );
	}

	// Set Hostname.
	String hostname( HOSTNAME );
	hostname += String( ESP.getChipId(), HEX );
	WiFi.hostname( hostname );

	char hostnameChar[hostname.length() + 1];
	memset( hostnameChar, 0, hostname.length() + 1 );

	for( uint8_t i = 0; i < hostname.length(); i++ )
		hostnameChar[i] = hostname.charAt( i );

	// MDNS.begin( hostnameChar );
	//  Add service to MDNS-SD
	// MDNS.addService( "http", "tcp", 80 );

	// Print hostname.
	Serial.println( "Hostname: " + hostname );

	// OTA update
	Serial.println( "Initializing OTA" );
	// ArduinoOTA.setPort( 8266 );
	ArduinoOTA.setHostname( hostnameChar );
	// ArduinoOTA.setPassword((const char *)"123");
	ArduinoOTA.onStart( []() {
		fill_solid( leds, NUM_LEDS, CRGB::Black );
		FastLED.show();
		OTA_in_progress = 1;
		Serial.println( "OTA Start" );
	} );
	ArduinoOTA.onEnd( []() {
		Serial.println( "\nOTA End" );
		fill_solid( leds, NUM_LEDS, CRGB::Green );
		FastLED.show();
	} );
	ArduinoOTA.onProgress( []( unsigned int progress, unsigned int total ) {
		fill_solid( leds, (int)( NUM_LEDS * ( (float)progress / (float)total ) ), CRGB::Blue );
		FastLED.show();
		Serial.printf( "OTA Progress: %u%%\r\n", ( progress / ( total / 100 ) ) );
	} );
	ArduinoOTA.onError( []( ota_error_t error ) {
		fill_solid( leds, NUM_LEDS, CRGB::Red );
		FastLED.show();
		Serial.printf( "OTA Error[%u]: ", error );
		if( error == OTA_AUTH_ERROR )
			Serial.println( "Auth Failed" );
		else if( error == OTA_BEGIN_ERROR )
			Serial.println( "Begin Failed" );
		else if( error == OTA_CONNECT_ERROR )
			Serial.println( "Connect Failed" );
		else if( error == OTA_RECEIVE_ERROR )
			Serial.println( "Receive Failed" );
		else if( error == OTA_END_ERROR )
			Serial.println( "End Failed" );
	} );
	ArduinoOTA.begin();

	httpUpdateServer.setup( &webServer );

	webServer.on( "/all", HTTP_GET, []() {
		String json = getFieldsJson( fields, fieldCount );
		webServer.send( 200, "text/json", json );
	} );

	webServer.on( "/fieldValue", HTTP_GET, []() {
		String name = webServer.arg( "name" );
		String value = getFieldValue( name, fields, fieldCount );
		webServer.send( 200, "text/json", value );
	} );

	webServer.on( "/fieldValue", HTTP_POST, []() {
		String name = webServer.arg( "name" );
		String value = webServer.arg( "value" );
		String newValue = setFieldValue( name, value, fields, fieldCount );
		webServer.send( 200, "text/json", newValue );
	} );

	webServer.on( "/power", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setPower( value.toInt() );
		sendInt( power );
	} );

	webServer.on( "/cooling", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		cooling = value.toInt();
		broadcastInt( "cooling", cooling );
		sendInt( cooling );
	} );

	webServer.on( "/sparking", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		sparking = value.toInt();
		broadcastInt( "sparking", sparking );
		sendInt( sparking );
	} );

	webServer.on( "/speed", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setSpeed( value.toInt() );
	} );

	webServer.on( "/twinkleSpeed", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		twinkleSpeed = value.toInt();
		if( twinkleSpeed < 0 )
			twinkleSpeed = 0;
		else if( twinkleSpeed > 8 )
			twinkleSpeed = 8;
		broadcastInt( "twinkleSpeed", twinkleSpeed );
		sendInt( twinkleSpeed );
	} );

	webServer.on( "/twinkleDensity", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		twinkleDensity = value.toInt();
		if( twinkleDensity < 0 )
			twinkleDensity = 0;
		else if( twinkleDensity > 8 )
			twinkleDensity = 8;
		broadcastInt( "twinkleDensity", twinkleDensity );
		sendInt( twinkleDensity );
	} );

	webServer.on( "/solidColor", HTTP_POST, []() {
		String r = webServer.arg( "r" );
		String g = webServer.arg( "g" );
		String b = webServer.arg( "b" );
		setSolidColor( r.toInt(), g.toInt(), b.toInt() );
		sendString( String( solidColor.r ) + "," + String( solidColor.g ) + "," + String( solidColor.b ) );
	} );

	webServer.on( "/pattern", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setPattern( value.toInt() );
		sendInt( currentPatternIndex );
	} );

	webServer.on( "/patternName", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setPatternName( value );
		sendInt( currentPatternIndex );
	} );

	webServer.on( "/palette", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setPalette( value.toInt() );
		sendInt( currentPaletteIndex );
	} );

	webServer.on( "/paletteName", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setPaletteName( value );
		sendInt( currentPaletteIndex );
	} );

	webServer.on( "/mqttServer", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setMqttServer( value );
		sendString( value );
	} );

	webServer.on( "/mqttPrefix", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setMqttPrefix( value );
		sendString( value );
	} );

	webServer.on( "/mqttEnabled", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setMqttEnabled( value.toInt() == 0 ? 0 : 0xAB );
		sendString( value );
	} );

	webServer.on( "/brightness", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setBrightness( value.toInt() );
		sendInt( brightness );
	} );

	webServer.on( "/autoplay", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setAutoplay( value.toInt() );
		sendInt( autoplay );
	} );

	webServer.on( "/autoplayDuration", HTTP_POST, []() {
		String value = webServer.arg( "value" );
		setAutoplayDuration( value.toInt() );
		sendInt( autoplayDuration );
	} );

	// list directory
	webServer.on( "/list", HTTP_GET, handleFileList );
	// load editor
	webServer.on( "/edit", HTTP_GET, []() {
		if( !handleFileRead( "/edit.htm" ) )
			webServer.send( 404, "text/plain", "FileNotFound" );
	} );
	// create file
	webServer.on( "/edit", HTTP_PUT, handleFileCreate );
	// delete file
	webServer.on( "/edit", HTTP_DELETE, handleFileDelete );
	// first callback is called after the request has ended with all parsed arguments
	// second callback handles file uploads at that location
	webServer.on(
	    "/edit", HTTP_POST, []() { webServer.send( 200, "text/plain", "" ); }, handleFileUpload );

	webServer.serveStatic( "/", SPIFFS, "/", "max-age=86400" );

	webServer.begin();
	Serial.println( "HTTP web server started" );

	webSocketsServer.begin();
	webSocketsServer.onEvent( webSocketEvent );
	Serial.println( "Web socket server started" );

	//  Serial.println("Starting UDP");
	//  udp.begin(udpLocalPort);
	//  Serial.print("Local port: ");
	//  Serial.println(udp.localPort());
	//
	//  Serial.println("waiting for sync");
	//  setSyncProvider(getNtpTime);
	unsigned long now = millis();
	autoPlayTimeout = now + ( autoplayDuration * 1000 );
	speedTime1 = now;
	speedTime2 = now;
}

void sendInt( uint8_t value ) { sendString( String( value ) ); }

void sendString( String value ) { webServer.send( 200, "text/plain", value ); }

void broadcastInt( String name, uint8_t value ) {
	String json = "{\"name\":\"" + name + "\",\"value\":" + String( value ) + "}";
	webSocketsServer.broadcastTXT( json );
}

void broadcastString( String name, String value ) {
	String json = "{\"name\":\"" + name + "\",\"value\":\"" + String( value ) + "\"}";
	webSocketsServer.broadcastTXT( json );
}

void setSpeed( uint8_t value ) {
	speed = value;
	broadcastInt( "speed", speed );
	sendInt( speed );
	publish_state();
}

void loop() {
	ArduinoOTA.handle();

	// do not continue if OTA update is in progress
	// OTA callbacks drive the LED display mode and OTA progress
	// in the background, the above call to LED.process() ensures
	// the OTA status is output to the LEDs
	if( OTA_in_progress )
		return;

	// Add entropy to random number generator; we use a lot of it.
	random16_add_entropy( random( 65535 ) );

	webSocketsServer.loop();
	webServer.handleClient();

	if( !mqtt_client.connected() && mqttEnabled ) {
		mqtt_reconnect();
	}
	if( mqttEnabled && mqtt_client.connected() )
		mqtt_client.loop();

	if( power == 0 ) {
		fill_solid( leds, NUM_LEDS, CRGB::Black );
		FastLED.show();
		// FastLED.delay(15);
		return;
	}

	// EVERY_N_SECONDS(10) {
	//   Serial.print( F("Heap: ") ); Serial.println(system_get_free_heap_size());
	// }

	unsigned long now = millis();

	// change to a new cpt-city gradient palette secondsPerPalette=10 (fix)
	// EVERY_N_SECONDS( secondsPerPalette )
	if( now > speedTime2 ) {
		speedTime2 = now + ( ( 255 - speed ) >> 2 ) * 1000; // between 0 and 64 sec switch time
		gCurrentPaletteNumber = addmod8( gCurrentPaletteNumber, 1, gGradientPaletteCount );
		gTargetPalette = gGradientPalettes[gCurrentPaletteNumber];

		paletteIndex = addmod8( paletteIndex, 1, paletteCount );
		targetPalette = palettes[paletteIndex];
	}

	// EVERY_N_MILLISECONDS( 40 )
	if( now > speedTime1 ) {
		speedTime1 = now + ( 255 - speed );
		// slowly blend the current palette to the next
		nblendPaletteTowardPalette( gCurrentPalette, gTargetPalette, 8 );
		nblendPaletteTowardPalette( currentPalette, targetPalette, 16 );
		gHue++; // slowly cycle the "base color" through the rainbow
	}

	if( autoplay && ( now > autoPlayTimeout ) ) {
		adjustPattern( true );
		autoPlayTimeout = now + ( autoplayDuration * 1000 );
	}

	// Call the current pattern function once, updating the 'leds' array
	patterns[currentPatternIndex].pattern();

	FastLED.show();

	// insert a delay to keep the framerate modest
	// FastLED.delay(1000 / FRAMES_PER_SECOND);
}

void webSocketEvent( uint8_t num, WStype_t type, uint8_t* payload, size_t length ) {

	switch( type ) {
	case WStype_DISCONNECTED:
		Serial.printf( "[%u] Disconnected!\n", num );
		break;

	case WStype_CONNECTED: {
		IPAddress ip = webSocketsServer.remoteIP( num );
		Serial.printf( "[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload );

		// send message to client
		// webSocketsServer.sendTXT(num, "Connected");
	} break;

	case WStype_TEXT:
		Serial.printf( "[%u] get Text: %s\n", num, payload );

		// send message to client
		// webSocketsServer.sendTXT(num, "message here");

		// send data to all connected clients
		// webSocketsServer.broadcastTXT("message here");
		break;

	case WStype_BIN:
		Serial.printf( "[%u] get binary length: %u\n", num, length );
		hexdump( payload, length );

		// send message to client
		// webSocketsServer.sendBIN(num, payload, lenght);
		break;
	}
}

// offsets of eeprom persisted settings
#define EEPROM_BRIGHTNESS 0
#define EEPROM_PATTERN 1
#define EEPROM_RED 2
#define EEPROM_GREEN 3
#define EEPROM_BLUE 4
#define EEPROM_POWER 5
#define EEPROM_AUTOPLAY 6
#define EEPROM_AUTOPLAY_DURATION 7
#define EEPROM_PALETTE 8
#define EEPROM_MQTT_ENABLED 9
#define EEPROM_MQTT_SERVER 20
#define EEPROM_MQTT_PREFIX 40

void loadSettings() {
	brightness = EEPROM.read( EEPROM_BRIGHTNESS );

	currentPatternIndex = EEPROM.read( EEPROM_PATTERN );
	if( currentPatternIndex < 0 )
		currentPatternIndex = 0;
	else if( currentPatternIndex >= patternCount )
		currentPatternIndex = patternCount - 1;

	byte r = EEPROM.read( EEPROM_RED );
	byte g = EEPROM.read( EEPROM_GREEN );
	byte b = EEPROM.read( EEPROM_BLUE );

	if( r == 0 && g == 0 && b == 0 ) {
	} else {
		solidColor = CRGB( r, g, b );
	}

	power = EEPROM.read( EEPROM_POWER );

	autoplay = EEPROM.read( EEPROM_AUTOPLAY );
	autoplayDuration = EEPROM.read( EEPROM_AUTOPLAY_DURATION );

	currentPaletteIndex = EEPROM.read( EEPROM_PALETTE );
	if( currentPaletteIndex < 0 )
		currentPaletteIndex = 0;
	else if( currentPaletteIndex >= paletteCount )
		currentPaletteIndex = paletteCount - 1;

	// check if mqtt setting where ever written
	if( EEPROM.read( EEPROM_MQTT_ENABLED ) == 0xAB ) { // magic number
		mqttServer = readStringFromEEPROM( EEPROM_MQTT_SERVER );
		mqttPrefix = readStringFromEEPROM( EEPROM_MQTT_PREFIX );
		mqttEnabled = 1;
	}
}

String hexstringFromCRGB( CRGB c ) {
	char hex[10];
	sprintf( hex, "#%02x%02x%02x", c.r, c.g, c.b );
	return String( hex );
}

//{"power":"off", "brightness":99, "autoplay":1, "autoplayDuration":255, "solidColor":"#ff00ff", "pattern":64,
//"palette":7}
void publish_state() {
	if( mqtt_client.connected() && mqttEnabled ) {
		String json = "{\"power\":\"" + String( power ? "on" : "off" ) + "\", \"brightness\":" + String( brightness ) +
		              ", \"autoplay\":" + String( autoplay ) + ", \"autoplayDuration\":" + String( autoplayDuration ) +
		              ", \"solidColor\":\"" + hexstringFromCRGB( solidColor ) + //"\"}";
		              "\", \"pattern\":" + String( currentPatternIndex ) +
		              ", \"palette\":" + String( currentPaletteIndex ) + ", \"speed\": " + String( speed ) + "}";
		int l = json.length();
		Serial.println( "MQTT msg: " + json + ", len: " + l );
		mqtt_client.publish( getTopic( mqttPrefix, "state" ), json.c_str() );
	}
}

void setPower( uint8_t value ) {
	power = value == 0 ? 0 : 1;

	EEPROM.write( EEPROM_POWER, power );
	EEPROM.commit();

	broadcastInt( "power", power );
	publish_state();
}

void setAutoplay( uint8_t value ) {
	autoplay = value == 0 ? 0 : 1;

	EEPROM.write( EEPROM_AUTOPLAY, autoplay );
	EEPROM.commit();

	broadcastInt( "autoplay", autoplay );
	publish_state();
}

void setAutoplayDuration( uint8_t value ) {
	autoplayDuration = value;

	EEPROM.write( EEPROM_AUTOPLAY_DURATION, autoplayDuration );
	EEPROM.commit();

	autoPlayTimeout = millis() + ( autoplayDuration * 1000 );

	broadcastInt( "autoplayDuration", autoplayDuration );
	publish_state();
}

void setSolidColor( CRGB color ) { setSolidColor( color.r, color.g, color.b ); }

void setSolidColor( uint8_t r, uint8_t g, uint8_t b ) {
	solidColor = CRGB( r, g, b );

	EEPROM.write( EEPROM_RED, r );
	EEPROM.write( EEPROM_GREEN, g );
	EEPROM.write( EEPROM_BLUE, b );
	EEPROM.commit();

	setPattern( patternCount - 1 );

	broadcastString( "color", String( solidColor.r ) + "," + String( solidColor.g ) + "," + String( solidColor.b ) );
	publish_state();
}

// increase or decrease the current pattern number, and wrap around at the ends
void adjustPattern( bool up ) {
	if( up )
		currentPatternIndex++;
	else
		currentPatternIndex--;

	// wrap around at the ends
	if( currentPatternIndex < 0 )
		currentPatternIndex = patternCount - 1;
	if( currentPatternIndex >= patternCount )
		currentPatternIndex = 0;

	if( autoplay == 0 ) {
		EEPROM.write( EEPROM_PATTERN, currentPatternIndex );
		EEPROM.commit();
	}

	broadcastInt( "pattern", currentPatternIndex );
}

void setPattern( uint8_t value ) {
	if( value >= patternCount )
		value = patternCount - 1;

	currentPatternIndex = value;

	if( autoplay == 0 ) {
		EEPROM.write( EEPROM_PATTERN, currentPatternIndex );
		EEPROM.commit();
	}

	broadcastInt( "pattern", currentPatternIndex );
	publish_state();
}

void setPatternName( String name ) {
	for( uint8_t i = 0; i < patternCount; i++ ) {
		if( patterns[i].name == name ) {
			setPattern( i );
			break;
		}
	}
}

void setPalette( uint8_t value ) {
	if( value >= paletteCount )
		value = paletteCount - 1;

	currentPaletteIndex = value;

	EEPROM.write( EEPROM_PALETTE, currentPaletteIndex );
	EEPROM.commit();

	broadcastInt( "palette", currentPaletteIndex );
	publish_state();
}

void setPaletteName( String name ) {
	for( uint8_t i = 0; i < paletteCount; i++ ) {
		if( paletteNames[i] == name ) {
			setPalette( i );
			break;
		}
	}
}

void writeStringToEEPROM( int addrOffset, const String& strToWrite ) {
	byte len = strToWrite.length();
	EEPROM.write( addrOffset, len );
	for( int i = 0; i < len; i++ ) {
		EEPROM.write( addrOffset + 1 + i, strToWrite[i] );
	}
}

String readStringFromEEPROM( int addrOffset ) {
	int newStrLen = EEPROM.read( addrOffset );
	char data[newStrLen + 1];
	for( int i = 0; i < newStrLen; i++ ) {
		data[i] = EEPROM.read( addrOffset + 1 + i );
	}
	data[newStrLen] = '\0';
	return String( data );
}

void adjustBrightness( bool up ) {
	if( up && brightnessIndex < brightnessCount - 1 )
		brightnessIndex++;
	else if( !up && brightnessIndex > 0 )
		brightnessIndex--;

	brightness = brightnessMap[brightnessIndex];

	FastLED.setBrightness( brightness );

	EEPROM.write( EEPROM_BRIGHTNESS, brightness );
	EEPROM.commit();

	broadcastInt( "brightness", brightness );
}

void mqttDisconnect() {
	Serial.println( "MQTT disconnect" );
	mqtt_client.disconnect();
}

void setMqttServer( String server ) {
	writeStringToEEPROM( EEPROM_MQTT_SERVER, server );
	mqttServer = server;
	mqttDisconnect();
	mqtt_client.setServer( server.c_str(), 1883 );
}

void setMqttPrefix( String prefix ) {
	writeStringToEEPROM( EEPROM_MQTT_PREFIX, prefix );
	mqttPrefix = prefix;
	mqttDisconnect();
}

void setMqttEnabled( int enabled ) {
	if( enabled && !mqttEnabled ) {
		mqtt_client.setServer( mqttServer.c_str(), 1883 );
	}
	if( !enabled && mqttEnabled ) {
		mqttDisconnect();
	}
	mqttEnabled = enabled;
	EEPROM.write( EEPROM_MQTT_ENABLED, enabled );
	EEPROM.commit();
}

void setBrightness( uint8_t value ) {
	if( value > 255 )
		value = 255;
	else if( value < 0 )
		value = 0;

	brightness = value;

	FastLED.setBrightness( brightness );

	EEPROM.write( EEPROM_BRIGHTNESS, brightness );
	EEPROM.commit();

	broadcastInt( "brightness", brightness );
	publish_state();
}

void strandTest() {
	static uint8_t i = 0;

	EVERY_N_SECONDS( 1 ) {
		i++;
		if( i >= NUM_LEDS )
			i = 0;
	}

	fill_solid( leds, NUM_LEDS, CRGB::Black );

	leds[i] = solidColor;
}

void showSolidColor() { fill_solid( leds, NUM_LEDS, solidColor ); }

void horizontalRainbow() {
	unsigned long t = millis();

	for( uint8_t i = 0; i < NUM_LEDS; i++ ) {
		leds[i] = CHSV( coordsX[i] + ( t / 10 ), 255, 255 );
	}
}

void verticalRainbow() {
	unsigned long t = millis();

	for( uint8_t i = 0; i < NUM_LEDS; i++ ) {
		leds[i] = CHSV( coordsY[i] + ( t / 10 ), 255, 255 );
	}
}

void diagonalRainbow() {
	unsigned long t = millis();

	for( uint8_t i = 0; i < NUM_LEDS; i++ ) {
		leds[i] = CHSV( coordsX[i] + coordsY[i] + ( t / 10 ), 255, 255 );
	}
}

void wave() {
	const uint8_t scale = 256 / kMatrixWidth;

	static uint8_t rotation = 0;
	static uint8_t theta = 0;
	static uint8_t waveCount = 1;

	uint8_t n = 0;

	switch( rotation ) {
	case 0:
		for( int x = 0; x < kMatrixWidth; x++ ) {
			n = quadwave8( x * 2 + theta ) / scale;
			setPixelXY( x, n, ColorFromPalette( currentPalette, x + gHue, 255, LINEARBLEND ) );
			if( waveCount == 2 )
				setPixelXY( x, maxY - n, ColorFromPalette( currentPalette, x + gHue, 255, LINEARBLEND ) );
		}
		break;

	case 1:
		for( int y = 0; y < kMatrixHeight; y++ ) {
			n = quadwave8( y * 2 + theta ) / scale;
			setPixelXY( n, y, ColorFromPalette( currentPalette, y + gHue, 255, LINEARBLEND ) );
			if( waveCount == 2 )
				setPixelXY( maxX - n, y, ColorFromPalette( currentPalette, y + gHue, 255, LINEARBLEND ) );
		}
		break;

	case 2:
		for( int x = 0; x < kMatrixWidth; x++ ) {
			n = quadwave8( x * 2 - theta ) / scale;
			setPixelXY( x, n, ColorFromPalette( currentPalette, x + gHue ) );
			if( waveCount == 2 )
				setPixelXY( x, maxY - n, ColorFromPalette( currentPalette, x + gHue, 255, LINEARBLEND ) );
		}
		break;

	case 3:
		for( int y = 0; y < kMatrixHeight; y++ ) {
			n = quadwave8( y * 2 - theta ) / scale;
			setPixelXY( n, y, ColorFromPalette( currentPalette, y + gHue, 255, LINEARBLEND ) );
			if( waveCount == 2 )
				setPixelXY( maxX - n, y, ColorFromPalette( currentPalette, y + gHue, 255, LINEARBLEND ) );
		}
		break;
	}

	dimAll( 254 );
	static unsigned long rotationTimer = 0;
	if( millis() > rotationTimer ) {
		rotationTimer = millis() + ( ( 256 - speed ) >> 4 ) * 1000;
		rotation = random( 0, 4 );
		// waveCount = random(1, 3);
	};
	static unsigned long thetaTimer = 0;
	if( millis() > thetaTimer ) {
		thetaTimer = millis() + ( 256 - speed ) >> 5;
		theta++;
	}
}

void pulse() {
	static unsigned long timer1 = 0;
	static unsigned long timer2 = 0;
	if( millis() > timer1 ) {
		timer1 = millis() + ( 256 - speed ) >> 5;
		dimAll( 200 );
	}

	if( millis() > timer2 ) {
		timer2 = millis() + ( 256 - speed ) >> 4;
		uint8_t maxSteps = 16;
		static uint8_t step = maxSteps;
		static uint8_t centerX = 0;
		static uint8_t centerY = 0;
		float fadeRate = 0.8;

		if( step >= maxSteps ) {
			centerX = random( kMatrixWidth );
			centerY = random( kMatrixWidth );
			step = 0;
		}

		if( step == 0 ) {
			drawCircle( centerX, centerY, step, ColorFromPalette( currentPalette, gHue, 255, LINEARBLEND ) );
			step++;
		} else {
			if( step < maxSteps ) {
				// initial pulse
				drawCircle( centerX, centerY, step,
				            ColorFromPalette( currentPalette, gHue, pow( fadeRate, step - 2 ) * 255, LINEARBLEND ) );

				// secondary pulse
				if( step > 3 ) {
					drawCircle( centerX, centerY, step - 3,
					            ColorFromPalette( currentPalette, gHue, pow( fadeRate, step - 2 ) * 255, LINEARBLEND ) );
				}

				step++;
			} else {
				step = -1;
			}
		}
	}
}

// Patterns from FastLED example DemoReel100:
// https://github.com/FastLED/FastLED/blob/master/examples/DemoReel100/DemoReel100.ino

void rainbow() {
	// FastLED's built-in rainbow generator
	fill_rainbow( leds, NUM_LEDS, gHue, 255 / NUM_LEDS );
}

void rainbowWithGlitter() {
	// built-in FastLED rainbow, plus some random sparkly glitter
	rainbow();
	addGlitter( 80 );
}

void rainbowSolid() { fill_solid( leds, NUM_LEDS, CHSV( gHue, 255, 255 ) ); }

void confetti() {
	// random colored speckles that blink in and fade smoothly
	fadeToBlackBy( leds, NUM_LEDS, 10 );
	int pos = random16( NUM_LEDS );
	// leds[pos] += CHSV( gHue + random8(64), 200, 255);
	leds[pos] += ColorFromPalette( palettes[currentPaletteIndex], gHue + random8( 64 ) );
}

void sinelon() {
	// a colored dot sweeping back and forth, with fading trails
	fadeToBlackBy( leds, NUM_LEDS, 20 );
	int pos = beatsin16( speed, 0, NUM_LEDS );
	static int prevpos = 0;
	CRGB color = ColorFromPalette( palettes[currentPaletteIndex], gHue, 255 );
	if( pos < prevpos ) {
		fill_solid( leds + pos, ( prevpos - pos ) + 1, color );
	} else {
		fill_solid( leds + prevpos, ( pos - prevpos ) + 1, color );
	}
	prevpos = pos;
}

void bpm() {
	// colored stripes pulsing at a defined Beats-Per-Minute (BPM)
	uint8_t beat = beatsin8( speed, 64, 255 );
	CRGBPalette16 palette = palettes[currentPaletteIndex];
	for( int i = 0; i < NUM_LEDS; i++ ) {
		leds[i] = ColorFromPalette( palette, gHue + ( i * 2 ), beat - gHue + ( i * 10 ) );
	}
}

void juggle() {
	static uint8_t numdots = 4;                // Number of dots in use.
	static uint8_t faderate = 2;               // How long should the trails be. Very low value = longer trails.
	static uint8_t hueinc = 255 / numdots - 1; // Incremental change in hue between each dot.
	static uint8_t thishue = 0;                // Starting hue.
	static uint8_t curhue = 0;                 // The current hue
	static uint8_t thissat = 255;              // Saturation of the colour.
	static uint8_t thisbright = 255;           // How bright should the LED/display be.
	static uint8_t basebeat = 5;               // Higher = faster movement.

	static uint8_t lastSecond = 99; // Static variable, means it's only defined once. This is our 'debounce' variable.
	static unsigned long timer1 = 0;
	uint8_t secondHand = 0;
	//( millis() / 1000 ) % 30; // IMPORTANT!!! Change '30' to a different value to change duration of the loop.
	if( millis() > timer1 ) {
		timer1 = millis() + ( ( 256 - speed ) >> 4 ) * 1000;
		if( lastSecond != secondHand ) { // Debounce to make sure we're not repeating an assignment.
			lastSecond = secondHand;
			switch( secondHand ) {
			case 0:
				numdots = 1;
				basebeat = 20;
				hueinc = 16;
				faderate = 2;
				thishue = 0;
				break; // You can change values here, one at a time , or altogether.
			case 10:
				numdots = 4;
				basebeat = 10;
				hueinc = 16;
				faderate = 8;
				thishue = 128;
				break;
			case 20:
				numdots = 8;
				basebeat = 3;
				hueinc = 0;
				faderate = 8;
				thishue = random8();
				break; // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
			case 30:
				secondHand = 0;
				break;
			}
			secondHand += 1;
		}
	}
	// Several colored dots, weaving in and out of sync with each other
	curhue = thishue; // Reset the hue values.
	fadeToBlackBy( leds, NUM_LEDS, faderate );
	for( int i = 0; i < numdots; i++ ) {
		// beat16 is a FastLED 3.1 function
		leds[beatsin16( basebeat + i + numdots, 0, NUM_LEDS )] += CHSV( gHue + curhue, thissat, thisbright );
		curhue += hueinc;
	}
}

void fire() { heatMap( HeatColors_p, true ); }

void water() { heatMap( IceColors_p, false ); }

// Pride2015 by Mark Kriegsman: https://gist.github.com/kriegsman/964de772d64c502760e5
// This function draws rainbows with an ever-changing,
// widely-varying set of parameters.
void pride() {
	static uint16_t sPseudotime = 0;
	static uint16_t sLastMillis = 0;
	static uint16_t sHue16 = 0;

	uint8_t sat8 = beatsin88( 87, 220, 250 );
	uint8_t brightdepth = beatsin88( 341, 96, 224 );
	uint16_t brightnessthetainc16 = beatsin88( 203, ( 25 * 256 ), ( 40 * 256 ) );
	uint8_t msmultiplier = beatsin88( 147, 23, 60 );

	uint16_t hue16 = sHue16; // gHue * 256;
	uint16_t hueinc16 = beatsin88( 113, 1, 3000 );

	uint16_t ms = millis();
	uint16_t deltams = ms - sLastMillis;
	sLastMillis = ms;
	sPseudotime += deltams * msmultiplier;
	sHue16 += deltams * beatsin88( 400, 5, 9 );
	uint16_t brightnesstheta16 = sPseudotime;

	for( uint16_t i = 0; i < NUM_LEDS; i++ ) {
		hue16 += hueinc16;
		uint8_t hue8 = hue16 / 256;

		brightnesstheta16 += brightnessthetainc16;
		uint16_t b16 = sin16( brightnesstheta16 ) + 32768;

		uint16_t bri16 = (uint32_t)( (uint32_t)b16 * (uint32_t)b16 ) / 65536;
		uint8_t bri8 = (uint32_t)( ( (uint32_t)bri16 ) * brightdepth ) / 65536;
		bri8 += ( 255 - brightdepth );

		CRGB newcolor = CHSV( hue8, sat8, bri8 );

		uint16_t pixelnumber = i;
		pixelnumber = ( NUM_LEDS - 1 ) - pixelnumber;

		nblend( leds[pixelnumber], newcolor, 64 );
	}
}

// based on FastLED example Fire2012WithPalette:
// https://github.com/FastLED/FastLED/blob/master/examples/Fire2012WithPalette/Fire2012WithPalette.ino
void heatMap( CRGBPalette16 palette, bool up ) {
	fill_solid( leds, NUM_LEDS, CRGB::Black );

	// Add entropy to random number generator; we use a lot of it.
	random16_add_entropy( random( 256 ) );

	// Array of temperature readings at each simulation cell
	static byte heat[256];

	byte colorindex;

	// Step 1.  Cool down every cell a little
	for( uint16_t i = 0; i < NUM_LEDS; i++ ) {
		heat[i] = qsub8( heat[i], random8( 0, ( ( cooling * 10 ) / NUM_LEDS ) + 2 ) );
	}

	// Step 2.  Heat from each cell drifts 'up' and diffuses a little
	for( uint16_t k = NUM_LEDS - 1; k >= 2; k-- ) {
		heat[k] = ( heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
	}

	// Step 3.  Randomly ignite new 'sparks' of heat near the bottom
	if( random8() < sparking ) {
		int y = random8( 7 );
		heat[y] = qadd8( heat[y], random8( 160, 255 ) );
	}

	// Step 4.  Map from heat cells to LED colors
	for( uint16_t j = 0; j < NUM_LEDS; j++ ) {
		// Scale the heat value from 0-255 down to 0-240
		// for best results with color palettes.
		colorindex = scale8( heat[j], 190 );

		CRGB color = ColorFromPalette( palette, colorindex );

		if( up ) {
			leds[j] = color;
		} else {
			leds[( NUM_LEDS - 1 ) - j] = color;
		}
	}
}

void addGlitter( uint8_t chanceOfGlitter ) {
	if( random8() < chanceOfGlitter ) {
		leds[random16( NUM_LEDS )] += CRGB::White;
	}
}

///////////////////////////////////////////////////////////////////////

// Forward declarations of an array of cpt-city gradient palettes, and
// a count of how many there are.  The actual color palette definitions
// are at the bottom of this file.
extern const TProgmemRGBGradientPalettePtr gGradientPalettes[];
extern const uint8_t gGradientPaletteCount;

uint8_t beatsaw8( accum88 beats_per_minute, uint8_t lowest = 0, uint8_t highest = 255, uint32_t timebase = 0,
                  uint8_t phase_offset = 0 ) {
	uint8_t beat = beat8( beats_per_minute, timebase );
	uint8_t beatsaw = beat + phase_offset;
	uint8_t rangewidth = highest - lowest;
	uint8_t scaledbeat = scale8( beatsaw, rangewidth );
	uint8_t result = lowest + scaledbeat;
	return result;
}

void colorWaves() { colorwaves( leds, NUM_LEDS, gCurrentPalette ); }

// ColorWavesWithPalettes by Mark Kriegsman: https://gist.github.com/kriegsman/8281905786e8b2632aeb
// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void colorwaves( CRGB* ledarray, uint16_t numleds, CRGBPalette16& palette ) {
	static uint16_t sPseudotime = 0;
	static uint16_t sLastMillis = 0;
	static uint16_t sHue16 = 0;

	// uint8_t sat8 = beatsin88( 87, 220, 250);
	uint8_t brightdepth = beatsin88( 341, 96, 224 );
	uint16_t brightnessthetainc16 = beatsin88( 203, ( 25 * 256 ), ( 40 * 256 ) );
	uint8_t msmultiplier = beatsin88( 147, 23, 60 );

	uint16_t hue16 = sHue16; // gHue * 256;
	uint16_t hueinc16 = beatsin88( 113, 300, 1500 );

	uint16_t ms = millis();
	uint16_t deltams = ms - sLastMillis;
	sLastMillis = ms;
	sPseudotime += deltams * msmultiplier;
	sHue16 += deltams * beatsin88( 400, 5, 9 );
	uint16_t brightnesstheta16 = sPseudotime;

	for( uint16_t i = 0; i < numleds; i++ ) {
		hue16 += hueinc16;
		uint8_t hue8 = hue16 / 256;
		uint16_t h16_128 = hue16 >> 7;
		if( h16_128 & 0x100 ) {
			hue8 = 255 - ( h16_128 >> 1 );
		} else {
			hue8 = h16_128 >> 1;
		}

		brightnesstheta16 += brightnessthetainc16;
		uint16_t b16 = sin16( brightnesstheta16 ) + 32768;

		uint16_t bri16 = (uint32_t)( (uint32_t)b16 * (uint32_t)b16 ) / 65536;
		uint8_t bri8 = (uint32_t)( ( (uint32_t)bri16 ) * brightdepth ) / 65536;
		bri8 += ( 255 - brightdepth );

		uint8_t index = hue8;
		// index = triwave8( index);
		index = scale8( index, 240 );

		CRGB newcolor = ColorFromPalette( palette, index, bri8 );

		uint16_t pixelnumber = i;
		pixelnumber = ( numleds - 1 ) - pixelnumber;

		nblend( ledarray[pixelnumber], newcolor, 128 );
	}
}

// Alternate rendering function just scrolls the current palette
// across the defined LED strip.
void palettetest( CRGB* ledarray, uint16_t numleds, const CRGBPalette16& gCurrentPalette ) {
	static uint8_t startindex = 0;
	startindex--;
	fill_palette( ledarray, numleds, startindex, ( 256 / NUM_LEDS ) + 1, gCurrentPalette, 255, LINEARBLEND );
}

void radialPaletteShift() {
	for( uint8_t i = 0; i < NUM_LEDS; i++ ) {
		// leds[i] = ColorFromPalette( gCurrentPalette, gHue + sin8(i*16), brightness);
		leds[i] = ColorFromPalette( gCurrentPalette, i + gHue, 255, LINEARBLEND );
	}
}

void radialPaletteShift2() {
	for( uint8_t i = 0; i < ringCount; i++ ) {
		for( uint8_t j = rings[i][0]; j <= rings[i][1]; j++ ) {
			leds[j] = ColorFromPalette( gCurrentPalette, ( i * 4 ) - beat8( speed ) );
		}
	}
}
unsigned long paletteArcsTimer = 0;

void paletteArcs() {
	dimAll( 254 );

	static uint8_t offset = 0;

	bool alternate = false;

	for( uint8_t i = 3; i < ringCount; i++ ) {
		uint8_t angle = beat8( i * 3 );

		if( alternate )
			angle = 255 - angle;

		fillRing256( i, ColorFromPalette( gCurrentPalette, ( i * ( 255 / ringCount ) ) + offset ), angle - 16, angle + 16 );

		alternate = !alternate;
	}

	if( millis() > paletteArcsTimer ) {
		paletteArcsTimer = millis() + ( 256 - speed ) >> 4;
		offset++;
	}
}

unsigned long decayingOrbitsTimer = 0;

void decayingOrbits() {
	dimAll( 240 );

	const uint8_t count = 12;
	static uint8_t positions[count];
	unsigned long now = millis();
	if( now > decayingOrbitsTimer ) {
		decayingOrbitsTimer = now + ( ( 255 - speed ) >> 5 );
		for( uint8_t i = 0; i < count; i++ ) {
			uint8_t pos = positions[i];

			if( pos == 255 && random8() < 1 )
				pos--;

			if( pos != 255 )
				pos--;

			if( pos != 255 )
				leds[NUM_LEDS - pos] = ColorFromPalette( gCurrentPalette, ( 255 / count ) * i );

			positions[i] = pos;
		}
	}
}
