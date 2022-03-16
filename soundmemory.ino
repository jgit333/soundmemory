#include <FS.h>
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioFileSourceSPIFFS.h"
#include <Adafruit_NeoPixel.h>

#define DEBOUNCE_TIME_MS 100 //Required to prevent double button events
#define KEYPAD_SCAN_INTERVAL_MS 2
#define N_PIXELS N_BUTTONS
#define NEO_PIN 1
#define N_PAIRS 6
#define GAIN 0.03 //Volume of speaker
#define ANIMATION_INTERVAL_MS 100
#define PLAYING_PIXEL_INTERVAL_MS 250
#define BLINK_HEART_UPDATE_INTERVAL_MS 10
#define STARTUP_ANIMATION_INTERVAL_MS 500
#define INCORRECT_MOVE_COLOR Adafruit_NeoPixel::Color(255, 0, 0)
#define CORRECT_MOVE_COLOR Adafruit_NeoPixel::Color(0, 255, 0)
#define CURRENT_MOVE_COLOR Adafruit_NeoPixel::Color(0, 0, 255)
#define CORRECT_MOVE_MP3 "/correct_answer.mp3"
#define INCORRECT_MOVE_MP3 "/wrong_answer.mp3"
#define COMPLETED_MP3 "/cheer.mp3"
#define STARTUP_MP3 "/startup.mp3"
#define MP3_LIST "/1.mp3", \
  "/2.mp3", \
  "/3.mp3", \
  "/4.mp3", \
  "/5.mp3", \
  "/6.mp3"

const int ROW_PINS[] = {D0, D5, D6, D7};
const int COL_PINS[] = {D3, D2, D1};
const int N_ROWS = sizeof(ROW_PINS) / sizeof(ROW_PINS[0]);
const int N_COLS = sizeof(COL_PINS) / sizeof(COL_PINS[0]);
const int N_BUTTONS = N_ROWS * N_COLS;
const int PIXEL_MAPPING[N_PIXELS] = {
  11, 10, 9,
  8, 7, 6,
  5, 4, 3,
  2, 1, 0
};

enum State {
  PLAY_STARTUP_SOUND,
  WAIT_FIRST_MOVE,
  PLAY_FIRST_MOVE_SAMPLE,
  WAIT_SECOND_MOVE,
  PLAY_SECOND_MOVE_SAMPLE,
  CHECK_MOVES,
  PLAY_INCORRECT_MOVE_SOUND,
  PLAY_CORRECT_MOVE_SOUND,
  PLAY_COMPLETED_SOUND,
  DONE
};

Adafruit_NeoPixel pixels(N_PIXELS, NEO_PIN, NEO_RGB + NEO_KHZ800);
AudioGeneratorMP3 *mp3;
AudioOutputI2S *out;
AudioFileSourceSPIFFS *file;
State game_state;
String pair_files[] = {MP3_LIST};
unsigned long last_startup_animation;
int startup_animation_state;
bool playing;
bool playing_pixel_state;
bool first_time;
uint32_t PINK_COLOR_WHEEL[N_PIXELS];
int tuples[N_BUTTONS];
bool completed_buttons[N_BUTTONS];
int n_completed_buttons;
int first;
int second;
unsigned long last_press;
unsigned long last_scan;
unsigned long last_animation;
unsigned long last_play_pixel;
unsigned long last_intensity_update;
int color_pos;
int playing_pixel;
int heart_intensity;
int intensity_direction;


void shuffleArray(int *array, int size)
{
  randomSeed(millis());
  int last = 0;
  int temp = array[last];
  for (int i = 0; i < size; i++)
  {
    int index = random(size);
    array[last] = array[index];
    last = index;
  }
  array[last] = temp;
}

void initializeGame()
{
  shuffleArray(tuples, N_BUTTONS);
  for (int i = 0; i < N_BUTTONS; i++) {
    completed_buttons[i] = 0;
  }
  first = -1;
  second = -1;
  n_completed_buttons = 0;
  pixels.clear();
  pixels.show();
  if (first_time) {
    playSoundAndSetGameState(STARTUP_MP3, PLAY_STARTUP_SOUND);
    first_time = 0;
  } else {
    game_state = WAIT_FIRST_MOVE;
  }
}

void initializePinkColorwheel() {
  for (int i = 0; i < N_PIXELS; i++)
  {
    int r = 255;
    int g = 0;
    int b = i * 255 / (N_PIXELS - 1);
    PINK_COLOR_WHEEL[i] = Adafruit_NeoPixel::Color(r, g, b);
  }
}

void setup()
{
  pixels.begin();
  delay(1);
  pixels.clear();
  pixels.show();

  SPIFFS.begin();
  out = new AudioOutputI2S();
  out->SetGain(GAIN);
  mp3 = new AudioGeneratorMP3();

  initializePinkColorwheel();

  for (int r = 0; r < N_ROWS; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], HIGH);
  }
  for (int c = 0; c < N_COLS; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }
  for (int t = 0; t < N_BUTTONS; t++) {
    tuples[t] = t / 2;
  }

  playing = 0;
  last_press = millis();
  last_scan = millis();
  last_animation = millis();
  last_play_pixel = millis();
  last_intensity_update = millis();
  last_startup_animation = millis();

  startup_animation_state = 0;

  color_pos = 0;
  heart_intensity = 255;
  intensity_direction = -1;

  first_time = 1;
  initializeGame();
}

void handleFirstMove(int index) {
  if (completed_buttons[index]) {
    game_state = WAIT_FIRST_MOVE;
    return;
  }

  first = index;
  pixels.setPixelColor(PIXEL_MAPPING[first], CURRENT_MOVE_COLOR);
  pixels.show();
  int sample_index = tuples[first];
  playSoundAndSetGameState(sample_index, PLAY_FIRST_MOVE_SAMPLE);
}

void syncPixelsWithCompletedMoves() {
  for (int i = 0; i < N_PIXELS; i++) {
    pixels.setPixelColor(PIXEL_MAPPING[i], CORRECT_MOVE_COLOR * completed_buttons[i]);
  }
  pixels.show();
}

void handleCorrectMove() {
  completed_buttons[first] = 1;
  completed_buttons[second] = 1;
  n_completed_buttons++;
  syncPixelsWithCompletedMoves();
}

void fastForwardToNextMove(int index) {
  if (tuples[first] == tuples[second]) {
    handleCorrectMove();
    if (n_completed_buttons == N_PAIRS) {
      playSoundAndSetGameState(COMPLETED_MP3, PLAY_COMPLETED_SOUND);
    } else {
      handleFirstMove(index);
    }
  } else {
    syncPixelsWithCompletedMoves();
    handleFirstMove(index);
  }
}

void handleSecondMove(int index) {
  if (completed_buttons[index]) {
    game_state = WAIT_SECOND_MOVE;
    return;
  }

  pixels.setPixelColor(PIXEL_MAPPING[first], CURRENT_MOVE_COLOR);
  pixels.show();
  if (index != first) {
    second = index;
    int sample_index = tuples[second];
    playSoundAndSetGameState(sample_index, PLAY_SECOND_MOVE_SAMPLE);
  }
}

void handleButtonPress(int index) {
  if (completed_buttons[index] && n_completed_buttons < N_PAIRS) return;

  switch (game_state) {
    case PLAY_SECOND_MOVE_SAMPLE:
      fastForwardToNextMove(index);
      break;
    case PLAY_INCORRECT_MOVE_SOUND:
      syncPixelsWithCompletedMoves();
    case PLAY_CORRECT_MOVE_SOUND:
    case WAIT_FIRST_MOVE:
      handleFirstMove(index);
      break;
    case PLAY_FIRST_MOVE_SAMPLE:
    case WAIT_SECOND_MOVE:
      handleSecondMove(index);
      break;
    case DONE:
      initializeGame();
      break;
  }
}

bool debounceOk() {
  bool lastPressOutsideDebounce = (millis() - last_press) > DEBOUNCE_TIME_MS;
  last_press = millis();
  return lastPressOutsideDebounce;
}

void scanKeypad() {
  for (int r = 0; r < N_ROWS; r++) {
    // disable previous scan row
    digitalWrite(ROW_PINS[(r + N_ROWS - 1) % N_ROWS], HIGH);
    // enable new scan row
    digitalWrite(ROW_PINS[r], LOW);
    for (int c = 0; c < N_COLS; c++) {
      int index = c + r * N_COLS;
      if (digitalRead(COL_PINS[c]) == LOW) {
        if (debounceOk()) {
          handleButtonPress(index);
        }
      }
    }
  }
}

void handleKeypad() {
  if (millis() - last_scan > KEYPAD_SCAN_INTERVAL_MS) {
    last_scan = millis();
    scanKeypad();
  }
}

void playSoundAndSetGameState(const char *filename, State state) {
  if (playing)
  {
    mp3->stop();
    delete file;
  }
  file = new AudioFileSourceSPIFFS(filename);
  mp3->begin(file, out);
  playing = 1;
  game_state = state;
  switch (game_state) {
    case PLAY_FIRST_MOVE_SAMPLE:
      {
        playing_pixel = PIXEL_MAPPING[first];
        playing_pixel_state = 1;
      } break;
    case PLAY_SECOND_MOVE_SAMPLE:
      {
        playing_pixel = PIXEL_MAPPING[second];
        playing_pixel_state = 1;
      } break;
  }
}

void playSoundAndSetGameState(int index, State state) {
  const char *filename = pair_files[index].c_str();
  playSoundAndSetGameState(filename, state);
}

void handleIncorrectMove() {
  pixels.setPixelColor(PIXEL_MAPPING[first], INCORRECT_MOVE_COLOR);
  pixels.setPixelColor(PIXEL_MAPPING[second], INCORRECT_MOVE_COLOR);
  pixels.show();
  playSoundAndSetGameState(INCORRECT_MOVE_MP3, PLAY_INCORRECT_MOVE_SOUND);
}

void checkMove() {
  if (tuples[first] == tuples[second]) {
    handleCorrectMove();
    if (n_completed_buttons < N_PAIRS) {
      playSoundAndSetGameState(CORRECT_MOVE_MP3, PLAY_CORRECT_MOVE_SOUND);
    } else {
      playSoundAndSetGameState(COMPLETED_MP3, PLAY_COMPLETED_SOUND);
    }
  } else {
    handleIncorrectMove();
  }
}

void blinkHeart() {
  if ((millis() - last_intensity_update) > BLINK_HEART_UPDATE_INTERVAL_MS) {
    last_intensity_update = millis();
    heart_intensity += intensity_direction;
    pixels.fill(Adafruit_NeoPixel::Color(heart_intensity, 0, heart_intensity));
    pixels.show();
    switch (heart_intensity) {
      case 0:
      case 255:
        intensity_direction *= -1;
        break;
    }
  }
}

void animateStartup() {
  if ((millis() - last_startup_animation) > STARTUP_ANIMATION_INTERVAL_MS) {
    last_startup_animation = millis();
    for (int i = 0; i < N_PIXELS; i++) {
      int r = (((i + startup_animation_state) % 3) == 0) * 255;
      int g = 0;
      int b = 255;
      pixels.setPixelColor(i, r, g, b);
    }
    pixels.show();
    startup_animation_state = (startup_animation_state + 1) % 3;
  }
}

void blinkDuringMoveSample() {
  if ((millis() - last_play_pixel) > PLAYING_PIXEL_INTERVAL_MS) {
    last_play_pixel = millis();
    pixels.setPixelColor(playing_pixel, CURRENT_MOVE_COLOR * playing_pixel_state);
    pixels.show();
    playing_pixel_state = !playing_pixel_state;
  }
}

void animatePinkColorWheel() {
  if ((millis() - last_animation) > ANIMATION_INTERVAL_MS) {
    last_animation = millis();
    for (int i = 0; i < N_PIXELS; i++) {
      pixels.setPixelColor(i, PINK_COLOR_WHEEL[(i + color_pos) % N_PIXELS]);
    }
    pixels.show();
    color_pos = (color_pos + 1) % N_PIXELS;
  }
}

void handleMp3Stopped() {
  mp3->stop();
  delete file;
  file = NULL;
  playing = 0;

  switch (game_state) {
    case PLAY_STARTUP_SOUND:
      {
        syncPixelsWithCompletedMoves();
        game_state = WAIT_FIRST_MOVE;
      } break;
    case PLAY_FIRST_MOVE_SAMPLE:
      {
        pixels.setPixelColor(PIXEL_MAPPING[first], CURRENT_MOVE_COLOR);
        pixels.show();
        game_state = WAIT_SECOND_MOVE;
      }  break;
    case PLAY_SECOND_MOVE_SAMPLE:
      game_state = CHECK_MOVES;
      break;
    case PLAY_CORRECT_MOVE_SOUND:
      game_state = WAIT_FIRST_MOVE;
      break;
    case PLAY_INCORRECT_MOVE_SOUND:
      {
        syncPixelsWithCompletedMoves();
        game_state = WAIT_FIRST_MOVE;
      } break;
    case PLAY_COMPLETED_SOUND:
      game_state = DONE;
      break;
  }
}

void handleEvents() {
  switch (game_state) {
    case PLAY_STARTUP_SOUND:
      animateStartup();
      break;
    case PLAY_COMPLETED_SOUND:
      animatePinkColorWheel();
      break;
    case PLAY_FIRST_MOVE_SAMPLE:
    case PLAY_SECOND_MOVE_SAMPLE:
      blinkDuringMoveSample();
      break;
    case CHECK_MOVES:
      checkMove();
      break;
    case DONE:
      blinkHeart();
      break;
  }
}

void loop()
{
  handleKeypad();

  if (playing && mp3->isRunning()) {
    if (mp3->loop())
    {
      handleEvents();
    } else {
      handleMp3Stopped();
    }
  } else {
    handleEvents();
  }
}
