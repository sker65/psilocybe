// Music reactive effects.
//
// Music Assistant (plugin "WLED Audio Sync") sends the audio analysis as WLED compatible "Audio Sync" packets,
// ~40 per second, UDP multicast to 239.0.0.1:11988. Packet v2, 44 bytes, little endian:
//   0  "00002\0"   header
//   8  float       volume raw      (~0-255)
//   12 float       volume smoothed (~0-255)
//   16 uint8       samplePeak: 1 in the packet in which a strong onset was detected (rare)
//   18 uint8[16]   frequency bands 0-255, index 0-3 = bass
//   36 float       magnitude of the strongest frequency
//   40 float       strongest frequency in Hz
//
// Beats are detected here from the bass (like the ESPHome effect "Musik 2 Bass-Beat"): bass clearly above its
// moving average, at most one beat per 250 ms.

#include <WiFiUdp.h>

#define MUSIC_PORT 11988
#define MUSIC_PACKET_SIZE 44
#define MUSIC_TIMEOUT_MS 2000 // no packets for this long = music paused/stopped

WiFiUDP musicUdp;
IPAddress musicUdpIp; // local IP the multicast group was joined with, 0 = not joined

struct MusicState {
	uint32_t rxMs = 0;        // time of the last valid packet
	uint32_t lastBeatMs = 0;
	float volume = 0;         // smoothed volume 0..1
	float bass = 0;           // mean of the bass bands 0..1
	float bassAvg = 0;        // moving average of bass (~0.7 s)
	uint8_t bands[16] = { 0 };
	bool beat = false;        // set on a beat, consumed once per frame
	bool frameBeat = false;   // there was a beat in the current frame
	float env = 0;            // pulse envelope, 1 on a beat, then decays
	uint8_t hue = 0;          // color index, steps on every beat
	uint32_t packets = 0;     // statistics for /info
	uint32_t beats = 0;
} music;

// global music mode for the regular effects
enum MusicMode : uint8_t { MusicOff = 0, MusicBrightness = 1, MusicSpeed = 2, MusicBrightnessSpeed = 3 };
uint8_t musicMode = MusicOff;
const uint8_t musicModeCount = 4;
const char* const musicModeNames[musicModeCount] = { "Off", "Brightness", "Speed", "Brightness + Speed" };

bool musicLive() { return music.rxMs != 0 && millis() - music.rxMs < MUSIC_TIMEOUT_MS; }

static float musicFloat( const uint8_t* p ) {
	float f;
	memcpy( &f, p, sizeof( f ) );
	return f;
}

static void musicParse( const uint8_t* d ) {
	uint32_t now = millis();
	music.rxMs = now;
	music.packets++;

	float smth = musicFloat( d + 12 );
	if( !( smth >= 0 ) ) // NaN
		smth = 0;
	music.volume = constrain( smth / 255.0f, 0.0f, 1.0f );
	memcpy( music.bands, d + 18, 16 );
	music.bass = ( d[18] + d[19] + d[20] + d[21] ) / ( 4.0f * 255.0f );

	// bass beat: 35 % above its average, above a minimum level, at most every 250 ms
	if( music.bass > 0.15f && music.bass > music.bassAvg * 1.35f && now - music.lastBeatMs >= 250 ) {
		music.beat = true;
		music.lastBeatMs = now;
		music.beats++;
	}
	music.bassAvg = music.bassAvg * 0.95f + music.bass * 0.05f;
}

// called every loop: (re)joins the multicast group and reads all pending packets
void handleMusicUdp() {
	if( WiFi.status() != WL_CONNECTED ) {
		musicUdpIp = IPAddress();
		return;
	}
	if( musicUdpIp != WiFi.localIP() ) { // first start or new IP after a reconnect
		musicUdp.stop();
		musicUdpIp = WiFi.localIP();
		musicUdp.beginMulticast( musicUdpIp, IPAddress( 239, 0, 0, 1 ), MUSIC_PORT );
	}

	uint8_t buf[MUSIC_PACKET_SIZE];
	int size;
	while( ( size = musicUdp.parsePacket() ) > 0 ) {
		if( size >= MUSIC_PACKET_SIZE && musicUdp.read( buf, MUSIC_PACKET_SIZE ) == MUSIC_PACKET_SIZE &&
		    memcmp( buf, "00002", 5 ) == 0 )
			musicParse( buf );
		musicUdp.flush();
	}
}

// once per frame (100 fps): consumes the beat and lets the envelope decay
// returns true if there was a beat in this frame
bool musicFrame() {
	bool beat = music.beat;
	music.beat = false;
	music.frameBeat = beat;
	if( beat ) {
		music.env = 1.0f;
		music.hue += 33; // ~47 degrees, like the ESPHome effect
	} else {
		music.env *= 0.93f; // half after ~90 ms
	}
	if( !musicLive() )
		music.env = 0;
	return beat;
}

// 0..1: pulse on beats, the bass keeps it up between them
float musicLevel() {
	if( !musicLive() )
		return 0;
	return max( music.env, music.bass * 0.8f );
}

// ---- modulation of the regular effects (setting "Music") ----

// brightness scale 0..255 for FastLED.show(): base glow 35 %, full on beats
uint8_t musicBrightnessScale() {
	if( !( musicMode & MusicBrightness ) || !musicLive() )
		return 255;
	return 90 + (uint8_t)( 165 * musicLevel() );
}

// speed while the effect is drawn: louder = faster, kicks on beats
uint8_t musicModulatedSpeed( uint8_t base ) {
	if( !( musicMode & MusicSpeed ) || !musicLive() )
		return base;
	float level = music.env * 0.5f + music.volume * 0.5f;
	return base + (uint8_t)( ( 255 - base ) * 0.8f * level );
}

// ---- music effects ----

// whole lamp in one color: pulses on bass beats, the color steps on every beat (ESPHome "Musik 2")
void musicPulse() {
	bool live = musicLive();
	// base glow 0.45, with gamma 2.8 like ESPHome; without music only the glow in the last color
	float v = live ? 0.45f + 0.55f * musicLevel() : 0.45f;
	uint8_t value = (uint8_t)( 255.0f * powf( v, 2.8f ) );
	CRGB color = ColorFromPalette( currentPalette, music.hue, 255 );
	color.nscale8_video( value );
	fill_solid( leds, numLeds, color );
}

// spectrum from the middle outwards: low frequencies in the middle, each band in its own palette color
void musicSpectrum() {
	static uint8_t level[16];
	for( uint8_t b = 0; b < 16; b++ ) {
		uint8_t target = musicLive() ? music.bands[b] : 0;
		// fast attack, slow decay
		level[b] = target > level[b] ? target : qsub8( level[b], 6 );
	}
	uint16_t half = ( numLeds + 1 ) / 2;
	for( uint16_t i = 0; i < half; i++ ) {
		uint8_t b = i * 16 / half;
		CRGB color = ColorFromPalette( currentPalette, b * 16 + music.hue, scale8( level[b], level[b] ) );
		uint16_t center = numLeds / 2;
		if( center + i < numLeds )
			leds[center + i] = color;
		if( center >= i + 1 )
			leds[center - i - 1] = color;
	}
}

// level meter from the middle outwards with falling peak dots (like WLED "Gravimeter")
void musicMeter() {
	static float peak = 0;
	uint16_t half = ( numLeds + 1 ) / 2;
	float level = musicLive() ? max( music.volume, music.env * 0.8f ) : 0;
	float bar = level * half;
	if( bar > peak )
		peak = bar;
	else
		peak = max( 0.0f, peak - 0.15f );

	fadeToBlackBy( leds, numLeds, 64 );
	uint16_t center = numLeds / 2;
	for( uint16_t i = 0; i < half && i < bar; i++ ) {
		CRGB color = ColorFromPalette( currentPalette, i * 255 / half + music.hue );
		if( center + i < numLeds )
			leds[center + i] = color;
		if( center >= i + 1 )
			leds[center - i - 1] = color;
	}
	uint16_t p = min( (uint16_t)peak, (uint16_t)( half - 1 ) );
	if( peak > 0.5f ) {
		if( center + p < numLeds )
			leds[center + p] = CRGB::White;
		if( center >= p + 1 )
			leds[center - p - 1] = CRGB::White;
	}
}

// on every beat a pool of light appears at a random place and fades out (like WLED "Puddles")
void musicRipple() {
	bool beat = music.frameBeat;
	fadeToBlackBy( leds, numLeds, 4 + speed / 16 );
	if( beat ) {
		uint8_t size = 2 + (uint8_t)( music.bass * numLeds / 6 );
		uint16_t pos = random16( numLeds );
		CRGB color = ColorFromPalette( currentPalette, music.hue + random8( 32 ) );
		for( uint16_t i = 0; i < size && pos + i < numLeds; i++ )
			leds[pos + i] = color;
	}
}

// the color of the music (bass = red, mids = green, highs = blue) enters in the middle and travels
// outwards (like WLED "DJ Light"); speed = how fast it travels
void musicDjLight() {
	static uint32_t lastStep = 0;
	uint32_t now = millis();
	if( now - lastStep < 10 + ( 255 - speed ) / 4 )
		return;
	lastStep = now;

	uint16_t center = numLeds / 2;
	for( uint16_t i = numLeds - 1; i > center; i-- )
		leds[i] = leds[i - 1];
	for( uint16_t i = 0; i + 1 < center; i++ )
		leds[i] = leds[i + 1];

	CRGB color = CRGB::Black;
	if( musicLive() ) {
		// bass, mids and highs are usually all loud at the same time = white. Only the difference between
		// them makes the color (the common part is removed), the loudness makes the brightness.
		uint16_t low = music.bands[0] + music.bands[1] + music.bands[2] + music.bands[3];
		uint16_t mid = music.bands[6] + music.bands[7] + music.bands[8] + music.bands[9];
		uint16_t high = music.bands[12] + music.bands[13] + music.bands[14] + music.bands[15];
		uint16_t lowest = min( low, min( mid, high ) );
		low -= lowest;
		mid -= lowest;
		high -= lowest;
		uint16_t highest = max( low, max( mid, high ) );
		uint8_t value = (uint8_t)( 255 * musicLevel() );
		if( highest > 0 && value > 0 ) {
			color = CRGB( low * 255 / highest, mid * 255 / highest, high * 255 / highest );
			color.nscale8_video( scale8( value, value ) );
		}
	}
	leds[center] = color;
	if( center > 0 )
		leds[center - 1] = color;
}

// calm ambient light: palette colors that drift and swell slowly with the music (seconds, not beats).
// Louder = brighter and the colors move a bit faster, the sound (bass ... highs) shifts the colors.
void musicAmbient() {
	static float energy = 0.3f; // smoothed loudness 0..1, ~3 s
	static float tone = 0;      // smoothed "brightness" of the sound, 0 (bass) .. 15 (highs), ~8 s
	static float hueShift = 0;
	static uint32_t z = 0;      // noise time

	float targetEnergy = 0.3f;
	if( musicLive() ) {
		targetEnergy = max( music.volume, music.bass );
		uint32_t sum = 0, weighted = 0;
		for( uint8_t b = 0; b < 16; b++ ) {
			sum += music.bands[b];
			weighted += music.bands[b] * b;
		}
		if( sum > 0 )
			tone += ( (float)weighted / sum - tone ) * 0.002f;
	}
	energy += ( targetEnergy - energy ) * 0.004f;

	// noise time: base from the speed setting, faster with the music
	z += 1 + speed / 32 + (uint32_t)( energy * 6 );
	hueShift += 0.02f + energy * 0.05f;

	uint8_t value = 70 + (uint8_t)( 185 * energy );
	uint8_t offset = (uint8_t)hueShift + (uint8_t)( tone * 12 );
	for( uint16_t i = 0; i < numLeds; i++ ) {
		uint8_t n = inoise8( i * 24, z >> 2 );
		// widen the noise range (inoise8 stays mostly around the middle)
		n = qsub8( n, 16 );
		n = qadd8( n, scale8( n, 39 ) );
		leds[i] = ColorFromPalette( currentPalette, n + offset, value );
	}
}
