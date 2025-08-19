#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <vector> // Used for managing thesong playlist

// Core Audio and Blutooth Libraries
#include "BluetoothA2DPSource.h"       // For sending audio via Bluetooth (A2DP Source)
#include "AudioCodecs/CodecMP3Helix.h" // The Helix MP3 dcoder library
#include "AudioTools.h"                // The main audio toolkit from pschatzmann

// Library for easy button management
#include "OneButton.h"

// ================================================================= //
// --- Configuration ---
// ================================================================= //

// IMPORTANT: This is the name of your Bluetooh earbuds.
const char *BT_EARBUDS_NAME = "Mivi DuoPods K6"; 

// --- Hardware Pin Definitions ---
// SD Card SPI pins
#define SD_CS    5  // Chip Select
#define SPI_MOSI 23 // Master Out Slave In
#define SPI_MISO 19 // Master In Slave Out
#define SPI_SCK  18 // Serial Clock

// Button input pins
#define BTN_NEXT_PIN 13 // Button for Play/Next Track/Stop
#define BTN_PREV_PIN 14 // Button for Previous Track

// ================================================================= //
// --- Global Objects and Variables ---
// ================================================================= //

BluetoothA2DPSource a2dp_source; // The Bluetooth object that sends audio
MP3DecoderHelix mp3_decoder;     // The object that decodes MP3 data
AudioPlayer player(a2dp_source, mp3_decoder); // The main player object that links the decoder to Bluetooth

OneButton buttonNext(BTN_NEXT_PIN, true); // Button for next/play/stop
OneButton buttonPrev(BTN_PREV_PIN, true); // Button for previous

std::vector<String> playlist; // A dynamic list to hold the filenames of your MP3s
int currentTrackIndex = 0;    // Index to keep track of the current song in the playlist
bool isPlaying = false;      // Flag to track the current playback status

// ================================================================= //
// --- Function Prototypes ---
// ================================================================= //

// Declaring functions before they are used to avoid compiler errors.
void playFile(const char* filename);
void playNextTrack();
void playPrevTrack();
void stopPlayback();
void pausePlayback();

// ================================================================= //
// --- AVRCP Callback (Receives commands FROM earbuds) ---
// ================================================================= //

// This function runs automatically whenever your earbuds send a command.
void avrcp_callback(esp_avrc_playback_stat_t playback_status) {
  // Print the raw command code for debugging: Serial.printf("AVRCP repsonse: %d\n", playback_status);
  switch(playback_status) {
    case ESP_AVRC_PLAYBACK_STOPPED:
      Serial.println("AVRCP command received: STOP");
      stopPlayback();
      break;
    case ESP_AVRC_PLAYBACK_PLAYING:
      Serial.println("AVRCP command received: PLAY");
      // If stopped, this will start playback. If paused, it will resume.
      if (!isPlaying) {
          playFile(playlist[currentTrackIndex].c_str());
      } else {
          player.pause(false); // Resume playback
      }
      break;
    case ESP_AVRC_PLAYBACK_PAUSED:
      Serial.println("AVRCP command received: PAUSE");
      pausePlayback(); // Toggles pause/play state
      break;
    case ESP_AVRC_PLAYBACK_FWD_SEEK:
      Serial.println("AVRCP command received: FORWARD (Next)");
      playNextTrack();
      break;
    case ESP_AVRC_PLAYBACK_REV_SEEK:
      Serial.println("AVRCP command received: REVERSE (Previous)");
      playPrevTrack();
      break;
    default:
      Serial.println("AVRCP command received: (Other/Unknown)");
      break;
  }
}

// ================================================================= //
// --- Helper Functions ---
// ================================================================= //

// Scans the root directory of the SD card for .mp3 files and adds them to the playlist.
void buildPlaylist(fs::FS &fs, const char *dirname) {
  Serial.printf("Scanning directory for music: %s\n", dirname);
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("-> Failed to open directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory() && (String(file.name()).endsWith(".mp3") || String(file.name()).endsWith(".MP3"))) {
      Serial.printf("  Found MP3: %s\n", file.name());
      playlist.push_back(String(file.name())); // Add the filename to our list
    }
    file = root.openNextFile();
  }
}

// ================================================================= //
// --- Main Setup Function (runs once on startup) ---
// ================================================================= //

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 SD Card Music Player (A2DP Source) ---");

  // --- Initialize SD Card ---
  Serial.println("Initializing SD card...");
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("FATAL: SD Card Mount Failed! Check wiring and format (FAT32). Halting.");
    while (1); // Stop the program if the SD card fails
  }
  buildPlaylist(SD, "/"); // Find songs and build the playlist
  if (playlist.empty()) {
    Serial.println("WARNING: No .mp3 files found on the SD card!");
  }

  // --- Initialize Buttons ---
  buttonNext.attachClick(playNextTrack);        // Single click -> Next Track / Play
  buttonNext.attachLongPressStart(stopPlayback);// Long press -> Stop
  buttonPrev.attachClick(playPrevTrack);        // Single click -> Previous Track
  Serial.println("Buttons initialized.");

  // --- Initialize Bluetooth ---
  Serial.printf("Starting Bluetooth... Attempting to connect to '%s'\n", BT_EARBUDS_NAME);
  a2dp_source.set_avrcp_callback(avrcp_callback); // Register the function to handle earbud commands
  a2dp_source.start(BT_EARBUDS_NAME);
}

// ================================================================= //
// --- Main Loop (runs continuously) ---
// ================================================================= //

void loop() {
  buttonNext.tick();
  buttonPrev.tick();
  
  if (isPlaying && player.isPaused() == false) {
    if (!player.copy()) { // copy() decodes a chunk of MP3 and sends it. Returns false when done.
      Serial.println("Song finished, playing next automatically.");
      playNextTrack();
    }
  }
}

// ================================================================= //
// --- Audio Control Functions ---
// ================================================================= //

// Plays a specific file from the SD card
void playFile(const char* filename) {
  if (isPlaying) {
    player.stop();
  }
  Serial.printf("Playing file: %s\n", filename);
  player.begin(SD.open(filename));
  if (player.isOk()) {
    isPlaying = true;
  } else {
    Serial.println("ERROR: Failed to start player. Check file or SD card.");
    isPlaying = false;
  }
}

// Logic for the "Next Track" button (or Play if stopped)
void playNextTrack() {
  if (playlist.empty() || !a2dp_source.is_connected()) return;
  
  if (!isPlaying) {
    playFile(playlist[currentTrackIndex].c_str());
    return;
  }
  
  currentTrackIndex++;
  if (currentTrackIndex >= playlist.size()) {
    currentTrackIndex = 0; // Loop back to the beginning
  }
  playFile(playlist[currentTrackIndex].c_str());
}

// Logic for the "Previous Track" button
void playPrevTrack() {
  if (playlist.empty() || !isPlaying || !a2dp_source.is_connected()) return;
  
  currentTrackIndex--;
  if (currentTrackIndex < 0) {
    currentTrackIndex = playlist.size() - 1; // Loop back to the end
  }
  playFile(playlist[currentTrackIndex].c_str());
}

// Stops playback completely
void stopPlayback() {
  if (isPlaying) {
    player.stop();
    isPlaying = false;
    Serial.println("Playback stopped by user.");
  }
}

// Toggles the pause state
void pausePlayback() {
  if (isPlaying) {
    player.pause(!player.isPaused());
  }
}
