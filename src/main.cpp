#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// Core Audio and Bluetooth Libraries
#include "BluetoothA2DPSource.h"
#include "AudioCodecs/CodecWAV.h" // Use the standard WAV decoder
#include "AudioTools.h"

// Library for easy button management
#include "OneButton.h"

// ================================================================= //
// --- Configuration ---
// ================================================================= //
const char *BT_EARBUDS_NAME = "Mivi DuoPods K6"; 

// --- Hardware Pin Definitions ---
#define SD_CS    5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK  18
#define BTN_NEXT_PIN 13
#define BTN_PREV_PIN 14

// ================================================================= //
// --- Global Objects and Variables ---
// ================================================================= //
BluetoothA2DPSource a2dp_source;
WAVDecoder wav_decoder; // <<<< CHANGED from MP3DecoderHelix to WAVDecoder
AudioPlayer player(a2dp_source, wav_decoder); // The player now uses the WAV decoder

OneButton buttonNext(BTN_NEXT_PIN, true);
OneButton buttonPrev(BTN_PREV_PIN, true);

std::vector<String> playlist;
int currentTrackIndex = 0;
bool isPlaying = false;

// ================================================================= //
// --- Function Prototypes ---
// ================================================================= //
void playFile(const char* filename);
void playNextTrack();
void playPrevTrack();
void stopPlayback();
void pausePlayback();

// ================================================================= //
// --- AVRCP Callback (Receives commands FROM earbuds) ---
// ================================================================= //
void avrcp_callback(esp_avrc_playback_stat_t playback_status) {
  switch(playback_status) {
    case ESP_AVRC_PLAYBACK_STOPPED:
      Serial.println("AVRCP: STOP");
      stopPlayback();
      break;
    case ESP_AVRC_PLAYBACK_PLAYING:
      Serial.println("AVRCP: PLAY");
      if (!isPlaying) { playFile(playlist[currentTrackIndex].c_str()); }
      else { player.pause(false); }
      break;
    case ESP_AVRC_PLAYBACK_PAUSED:
      Serial.println("AVRCP: PAUSE");
      pausePlayback();
      break;
    case ESP_AVRC_PLAYBACK_FWD_SEEK:
      Serial.println("AVRCP: FORWARD (Next)");
      playNextTrack();
      break;
    case ESP_AVRC_PLAYBACK_REV_SEEK:
      Serial.println("AVRCP: REVERSE (Previous)");
      playPrevTrack();
      break;
    default:
      Serial.println("AVRCP: (Other)");
      break;
  }
}

// ================================================================= //
// --- Helper Functions ---
// ================================================================= //
void buildPlaylist(fs::FS &fs, const char *dirname) {
  Serial.printf("Scanning for .wav files in: %s\n", dirname);
  File root = fs.open(dirname);
  if (!root) { Serial.println("-> Failed to open directory"); return; }
  
  File file = root.openNextFile();
  while (file) {
    // <<<< CHANGED to look for .wav and .WAV files
    if (!file.isDirectory() && (String(file.name()).endsWith(".wav") || String(file.name()).endsWith(".WAV"))) {
      Serial.printf("  Found: %s\n", file.name());
      playlist.push_back(String(file.name()));
    }
    file = root.openNextFile();
  }
}

// ================================================================= //
// --- Main Setup and Loop ---
// ================================================================= //
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 WAV Player (A2DP Source) ---");

  Serial.println("Initializing SD card...");
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("FATAL: SD Card Mount Failed! Halting.");
    while (1);
  }
  buildPlaylist(SD, "/");
  if (playlist.empty()) {
    Serial.println("WARNING: No .wav files found!");
  }

  buttonNext.attachClick(playNextTrack);
  buttonNext.attachLongPressStart(stopPlayback);
  buttonPrev.attachClick(playPrevTrack);
  Serial.println("Buttons initialized.");

  Serial.printf("Starting Bluetooth... Connecting to '%s'\n", BT_EARBUDS_NAME);
  a2dp_source.set_avrcp_callback(avrcp_callback);
  a2dp_source.start(BT_EARBUDS_NAME);
}

void loop() {
  buttonNext.tick();
  buttonPrev.tick();
  
  if (isPlaying && !player.isPaused()) {
    if (!player.copy()) {
      Serial.println("Song finished, playing next.");
      playNextTrack();
    }
  }
}

// ================================================================= //
// --- Audio Control Functions (No changes needed here) ---
// ================================================================= //
void playFile(const char* filename) {
  if (isPlaying) player.stop();
  Serial.printf("Playing file: %s\n", filename);
  player.begin(SD.open(filename));
  isPlaying = player.isOk();
  if (!isPlaying) Serial.println("ERROR: Failed to start player.");
}

void playNextTrack() {
  if (playlist.empty() || !a2dp_source.is_connected()) return;
  if (!isPlaying) { playFile(playlist[currentTrackIndex].c_str()); return; }
  
  currentTrackIndex++;
  if (currentTrackIndex >= playlist.size()) currentTrackIndex = 0;
  playFile(playlist[currentTrackIndex].c_str());
}

void playPrevTrack() {
  if (playlist.empty() || !isPlaying || !a2dp_source.is_connected()) return;
  
  currentTrackIndex--;
  if (currentTrackIndex < 0) currentTrackIndex = playlist.size() - 1;
  playFile(playlist[currentTrackIndex].c_str());
}

void stopPlayback() {
  if (isPlaying) {
    player.stop();
    isPlaying = false;
    Serial.println("Playback stopped.");
  }
}

void pausePlayback() {
  if (isPlaying) player.pause(!player.isPaused());
}
