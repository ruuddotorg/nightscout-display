#include <Adafruit_LEDBackpack.h>
#include <TimeLib.h>
#include <WiFi101.h>

// Note: Loosely based on WiFiWebClientRepeating example.

// Constants and configuration

const char kWiFiSsid[] = "YourWiFiSSID";
const char kWiFiPassphrase[] = "YourWiFiPassphrase";

const char kNightscoutHostname[] = "yournightscoutsite.azurewebsites.net";
const uint16_t kNightscoutPort = 443;  // 80 for http, 443 for https.

const int kTimeZone = -8;  // Hours from UTC (-8 is PST).  TODO: automatic DST adjustment

const int kLedBrightness = 0;  // 0..15
const int kLedRotation = 1;  // 1: USB on the left, 3: USB on the right
const long kStalenessThreshold = 15 * 60;  // Display "--" if Nightscout value is older than 15 minutes
const unsigned long kErrorTimeout = 5 * 60 * 1000;  // Display "Error" after 5 minutes of no connection

// Global variables

Adafruit_8x16minimatrix glucose_matrix;
Adafruit_8x16minimatrix clock_matrix;

// Bitmaps

const uint8_t NARROW_DIGITS[][8] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 0 (blank)
  { 0x00, 0x40, 0xc0, 0x40, 0x40, 0x40, 0x00, 0x00 },  // 1
  { 0x00, 0xc0, 0x40, 0xc0, 0x80, 0xc0, 0x00, 0x00 },  // 2
  { 0x00, 0xc0, 0x40, 0xc0, 0x40, 0xc0, 0x00, 0x00 },  // 3
  { 0x00, 0x40, 0xc0, 0xc0, 0x40, 0x40, 0x00, 0x00 },  // "4"
  { 0x00, 0xc0, 0x80, 0xc0, 0x40, 0xc0, 0x00, 0x00 },  // 5
  { 0x00, 0xc0, 0x80, 0xc0, 0xc0, 0xc0, 0x00, 0x00 },  // "6"
  { 0x00, 0xc0, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00 },  // 7
  { 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0x00 },  // "8"
  { 0x00, 0xc0, 0xc0, 0xc0, 0x40, 0xc0, 0x00, 0x00 },  // "9"
};

const uint8_t NORMAL_DIGITS[][8] = {
  { 0x00, 0xe0, 0xa0, 0xa0, 0xa0, 0xe0, 0x00, 0x00 },  // 0
  { 0x00, 0x40, 0xc0, 0x40, 0x40, 0x40, 0x00, 0x00 },  // 1
  { 0x00, 0xe0, 0x20, 0xe0, 0x80, 0xe0, 0x00, 0x00 },  // 2
  { 0x00, 0xe0, 0x20, 0x60, 0x20, 0xe0, 0x00, 0x00 },  // 3
  { 0x00, 0x20, 0xa0, 0xe0, 0x20, 0x20, 0x00, 0x00 },  // 4
  { 0x00, 0xe0, 0x80, 0xe0, 0x20, 0xe0, 0x00, 0x00 },  // 5
  { 0x00, 0xe0, 0x80, 0xe0, 0xa0, 0xe0, 0x00, 0x00 },  // 6
  { 0x00, 0xe0, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00 },  // 7
  { 0x00, 0xe0, 0xa0, 0xe0, 0xa0, 0xe0, 0x00, 0x00 },  // 8
  { 0x00, 0xe0, 0xa0, 0xe0, 0x20, 0xe0, 0x00, 0x00 },  // 9
};

const uint8_t ARROW_BITMAPS[][8] = {
  { 0x20, 0x70, 0xa8, 0x00, 0x20, 0x70, 0xa8, 0x00 },  // Double up
  { 0x00, 0x20, 0x70, 0xa8, 0x20, 0x20, 0x00, 0x00 },  // Single up
  { 0x00, 0x78, 0x18, 0x28, 0x48, 0x80, 0x00, 0x00 },  // 45-degree up
  { 0x00, 0x20, 0x10, 0xf8, 0x10, 0x20, 0x00, 0x00 },  // Flat
  { 0x00, 0x80, 0x48, 0x28, 0x18, 0x78, 0x00, 0x00 },  // 45-degree down
  { 0x00, 0x20, 0x20, 0xa8, 0x70, 0x20, 0x00, 0x00 },  // Single down
  { 0xa8, 0x70, 0x20, 0x00, 0xa8, 0x70, 0x20, 0x00 },  // Double down
};

const uint8_t ERROR_BITMAP[] = {
  0x00, 0x00, 0xe0, 0x00, 0x80, 0x00, 0xcd, 0xbb,
  0x89, 0x2a, 0xe9, 0x3a, 0x00, 0x00, 0x00, 0x00
};

// Display functions

void InitMatrix(Adafruit_8x16minimatrix *matrix, uint8_t i2c_address) {
  matrix->begin(i2c_address);
  matrix->clear();
  matrix->writeDisplay();
  matrix->setBrightness(kLedBrightness);
  matrix->setRotation(kLedRotation);
}

void DisplayClock(const time_t t) {
  if (timeStatus() == timeNotSet) {
    return;
  }

  const int h = hourFormat12(t);
  const int m = minute(t);

  clock_matrix.clear();
  clock_matrix.drawBitmap(0, 0, NARROW_DIGITS[h / 10], 8, 8, LED_ON);
  clock_matrix.drawBitmap(3, 0, NORMAL_DIGITS[h % 10], 8, 8, LED_ON);
  clock_matrix.drawPixel(7, 2, LED_ON);
  clock_matrix.drawPixel(7, 4, LED_ON);
  clock_matrix.drawBitmap(9, 0, NORMAL_DIGITS[m / 10], 8, 8, LED_ON);
  clock_matrix.drawBitmap(13, 0, NORMAL_DIGITS[m % 10], 8, 8, LED_ON);
  clock_matrix.writeDisplay();
}

void DisplayError() {
  glucose_matrix.clear();
  glucose_matrix.drawBitmap(0, 0, ERROR_BITMAP, 16, 8, LED_ON);
  glucose_matrix.writeDisplay();
}

void DisplayGlucose(const long now, const long sgv, const long trend, const long datetime) {
  glucose_matrix.clear();
  if (now - datetime > kStalenessThreshold) {
    // --
    glucose_matrix.drawFastHLine(3, 3, 3, LED_ON);
    glucose_matrix.drawFastHLine(7, 3, 3, LED_ON);
  } else {
    glucose_matrix.drawBitmap(0, 0, NARROW_DIGITS[sgv / 100], 8, 8, LED_ON);
    glucose_matrix.drawBitmap(3, 0, NORMAL_DIGITS[(sgv % 100) / 10], 8, 8, LED_ON);
    glucose_matrix.drawBitmap(7, 0, NORMAL_DIGITS[sgv % 10], 8, 8, LED_ON);
    if (trend >= 1 && trend <= 7) {
      glucose_matrix.drawBitmap(11, 0, ARROW_BITMAPS[trend-1], 8, 8, LED_ON);
    }
  }
  glucose_matrix.writeDisplay();
}

void WaitWithProgressBar(const int milliseconds) {
  const int milliseconds_per_dot = milliseconds / 16;
  for (int i = 0; i < 16; ++i) {
    glucose_matrix.drawPixel(i, 3, LED_ON);
    glucose_matrix.writeDisplay();
    delay(milliseconds_per_dot);
  }
  glucose_matrix.clear();
  glucose_matrix.writeDisplay();
}

// Network functions

void InitWiFi() {
  WiFi.setPins(8, 7, 4, 2);  // Adafruit Feather M0 WiFi pin assignments.

  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("No WiFi support.");
    while (true) ;  // Don't continue.
  }

  int status = WL_IDLE_STATUS;
  do {
    status = WiFi.begin(kWiFiSsid, kWiFiPassphrase);
    WaitWithProgressBar(5000);  // Wait ~5 seconds for connection.
  } while (status != WL_CONNECTED);

  LogWiFiStatus();
}

void LogWiFiStatus() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI (dBm): ");
  Serial.println(WiFi.RSSI());
}

bool ConnectToNightscout(WiFiClient* client) {
  if (kNightscoutPort == 443) {
    return client->connectSSL(kNightscoutHostname, kNightscoutPort);
  } else {
    return client->connect(kNightscoutHostname, kNightscoutPort);
  }
}

void PerformNightscoutRequest(WiFiClient* client) {
  client->stop();  // Cancel any previous connections.

  if (ConnectToNightscout(client)) {
    client->println("GET /pebble HTTP/1.1");
    client->print("Host: ");
    client->println(kNightscoutHostname);
    client->println("Connection: close");
    client->println();
  }
}

// Nightscout JSON "parsing" functions

// Very simplistic scanner for JSON character streams.
// Once the provided key is found in the stream, the subsequent characters
// (at most 10) are parsed as a decimal number.
class JsonScanner {
  public:
    // Initializes scanner.
    JsonScanner(const char* const key) : key_(key) {
      reset();
    }

    // Parse a single character from the stream.
    // Returns true if key is found and subsequent number is fully parsed.
    bool scan(const char c) {
      if (isspace(c)) {
        return false;
      }
      if (*p_) {
        if (*p_ == c) {
          ++p_;
        } else {
          p_ = key_;
        }
      } else if (digits_ > 0) {
        if (isdigit(c)) {
          value_ = value_ * 10 + (c - '0');
          --digits_;
        } else {
          digits_ = 0;
        }
        if (digits_ == 0) {
          return true;
        }
      }
      return false;
    }

    // Returns the parsed value.
    long value() const {
      return value_;
    }

    // Resets state of the scanner.
    void reset() {
      p_ = key_;
      value_ = 0;
      digits_ = 10;
    }

private:
  const char* const key_;
  const char* p_;
  long value_;
  int digits_;
};

// Parser for JSON stream from Nightscout. Finds the 4 fields we're interested
// in and updates the glucose display once all of them are found.  Also updates
// system time from Nightscout time.
class NightscoutParser {
  public:
    NightscoutParser()
      : now_("\"now\":"),
        sgv_("\"sgv\":\""),  // Extra quotes because sgv is a string.
        trend_("\"trend\":"),
        datetime_("\"datetime\":"),
        last_update_(0) {
      reset();
    }

    void reset() {
      found = 0;
      now_.reset();
      sgv_.reset();
      trend_.reset();
      datetime_.reset();
    }

    void parse(const char c) {
      if (found == 4) {
        return;  // Already found everything in a previous call.
      }

      if (now_.scan(c)) {
        ++found;
        // Update time, and display clock if not set previously.
        const bool update_display = (timeStatus() == timeNotSet);
        const time_t t = now_.value() + 3600 * kTimeZone;
        setTime(t);
        if (update_display) {
          DisplayClock(t);
        }
      }
      if (sgv_.scan(c)) {
        ++found;
      }
      if (trend_.scan(c)) {
        ++found;
      }
      if (datetime_.scan(c)) {
        ++found;
      }

      if (found == 4) {
        DisplayGlucose(now_.value(), sgv_.value(), trend_.value(), datetime_.value());
        last_update_ = millis();
      }
    }

    long last_update() const {
      return last_update_;
    }

  private:
    JsonScanner now_;
    JsonScanner sgv_;
    JsonScanner trend_;
    JsonScanner datetime_;
    int found;
    unsigned long last_update_;
};

// Arduino main functions

void setup() {
  Serial.begin(9600);
  InitMatrix(&glucose_matrix, 0x70);
  InitMatrix(&clock_matrix, 0x71);
  InitWiFi();

  adjustTime(-now());  // Hack: resets time to 00:00, but doesn't consider it synced yet.
}

void loop() {
  static bool top_of_minute = false;
  static WiFiClient http_client;
  static NightscoutParser parser;

  // Display clock and connect to Nightscout at top of each minute.
  const time_t t = now();
  if (second(t) != 0) {
    top_of_minute = false;
  } else if (!top_of_minute) {
    top_of_minute = true;
    DisplayClock(t);
    if (millis() - parser.last_update() >= kErrorTimeout) {
      DisplayError();
    }
    PerformNightscoutRequest(&http_client);
    parser.reset();
  }

  // Process incoming data from Nightscout.
  while (http_client.available()) {
    parser.parse(http_client.read());
  }
}
