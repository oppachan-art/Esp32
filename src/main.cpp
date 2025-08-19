#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "OneButton.h"
#include "BluetoothA2DPSource.h"
#include "Audio.h" // The new, simpler audio library header

// ================================================================= //
// --- Configuration ---
// ================================================================= //
const char *BT_EARBUDS_NAME = "Mivi DuoPods K6"; 

// --- Hardware Pin Definitions ---
// Note: This new library uses the standard VSPI pins for SD card
#define SD_CS    5
#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23
#define BTN_NEXT_PIN 13
#define BTN_PREV_PIN 14

// ================================================================= //
// --- Global Objects and Variables ---
// ================================================================= //
BluetoothA2DPSource a2dp_source;
Audio audio; // The new audio engine object!

OneButton buttonNext(BTN_NEXT_PIN, true);
OneButton buttonPrev(BTN_PREV_PIN, true);

std::vector<String> playlist;
int currentTrackIndex = 0;

// ================================================================= //
// --- Function Prototypes ---
// ================================================================= //
void playNextTrack();
void playPrevTrack();
void stopPlayback();

// ================================================================= //
// --- Library Status Reporting ---
// ================================================================= //
// This function is called by the audio library to report its status
void audio_info(const char *info){
    Serial.print("audio_info: ");
    Serial.println(info);
}

// ================================================================= //
// --- Main Setup and Loop ---
// ================================================================= //
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 Simple WAV Player ---");

  // --- Initialize SD Card ---
  // The new library prefers its own SPI setup
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("FATAL: SD Card Mount Failed! Halting.");
    while (1);
  }

  // --- Build Playlist ---
  File root = SD.open("/");
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory() && (String(file.name()).endsWith(".wav") || String(file.name()).endsWith(".WAV"))) {
      Serial.printf("  Found: %s\n", file.name());
      playlist.push_back(String(file.name()));
    }
    file = root.openNextFile();
  }
  if (playlist.empty()) Serial.println("WARNING: No .wav files found!");

  // --- Initialize Bluetooth ---
  Serial.printf("Starting Bluetooth... Connecting to '%s'\n", BT_EARBUDS_NAME);
  a2dp_source.start(BT_EARBUDS_NAME);
  
  // --- Configure the NEW Audio Engine ---
  // The audio library needs to know where to send the sound. We tell it to send to our Bluetooth object.
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT); // Standard I2S pins, not used for BT
  audio.setConnection(A2DP_SINK); // <<< CRITICAL: Tell audio to output to Bluetooth!
  audio.setA2DPSource(&a2dp_source);

  // --- Initialize Buttons ---
  buttonNext.attachClick(playNextTrack);
  buttonNext.attachLongPressStart(stopPlayback);
  buttonPrev.attachClick(playPrevTrack);
  Serial.println("Buttons initialized. Ready to play.");
}

void loop() {
  audio.loop(); // The new library requires this to be called continuously
  buttonNext.tick();
  buttonPrev.tick();
}

// ================================================================= //
// --- Audio Control Functions (Rewritten for the new library) ---
// ================================================================= //
void playNextTrack() {
  if (playlist.empty() || !a2dp_source.is_connected()) return;
  
  currentTrackIndex++;
  if (currentTrackIndex >= playlist.size()) {
    currentTrackIndex = 0; // Loop back
  }
  
  String filepath = "/" + playlist[currentTrackIndex];
  Serial.printf("Playing next: %s\n", filepath.c_str());
  audio.connecttoFS(SD, filepath.c_str());
}

void playPrevTrack() {
  if (playlist.empty() || !audio.isRunning() || !a2dp_source.is_connected()) return;
  
  currentTrackIndex--;
  if (currentTrackIndex < 0) {
    currentTrackIndex = playlist.size() - 1; // Loop back
  }

  String filepath = "/" + playlist[currentTrackIndex];
  Serial.printf("Playing previous: %s\n", filepath.c_str());
  audio.connecttoFS(SD, filepath.c_str());
}

void stopPlayback() {
  if (audio.isRunning()) {
    Serial.println("Playback stopped.");
    audio.stopSong();
  }
}
