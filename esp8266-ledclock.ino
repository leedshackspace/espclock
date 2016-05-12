#include <Ticker.h>

#include <Time.h>
#include <EEPROM.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include "settings.h"
#include "mainPage.h"
#include "espclock.h"

#define SETUP_PIN 3

#define MODE_SETUP 0
#define MODE_CLOCK 1
int clockMode;

#define OPEN_POLL_SECONDS 5

static time_t displayed_time;
static int skew;
static bool half;
static int good_ticks;
static bool space_open = true;


#define TICK_MS 5
#define MAX_GOOD_TICKS 10
#define JITTER 200

#define JUMP_SECONDS 30

ESP8266WebServer server (80);

String httpUpdateResponse;

void handleRoot() {
  String s = MAIN_page;
  s.replace("@@SSID@@", settings.ssid);
  s.replace("@@PSK@@", settings.psk);
  s.replace("@@TZ@@", String(settings.timezone));
  s.replace("@@HOUR@@", String(hour()));
  s.replace("@@MIN@@", String(minute()));
  s.replace("@@NTPSRV@@", settings.timeserver);
  s.replace("@@NTPINT@@", String(settings.interval));
  s.replace("@@SYNCSTATUS@@", timeStatus() == timeSet ? "OK" : "Overdue");
  s.replace("@@CLOCKNAME@@", settings.name);
  s.replace("@@UPDATERESPONSE@@", httpUpdateResponse);
  httpUpdateResponse = "";
  server.send(200, "text/html", s);
}


void setupAP() {
  clockMode = MODE_SETUP;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_NAME);
  displayAP();
}

void setupSTA()
{
  char ssid[32];
  char psk[64];
  memset(ssid, 0, 32);
  memset(psk, 0, 64);
  displayBusy(1);

  clockMode = MODE_CLOCK;
  WiFi.mode(WIFI_STA);
  settings.ssid.toCharArray(ssid, 32);
  settings.psk.toCharArray(psk, 64);
  if (settings.psk.length()) {
    WiFi.begin(ssid, psk);
  } else {
    WiFi.begin(ssid);
  }
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  stopDisplayBusy();
  displayDash();
}

void setupWiFi() {
  settings.Load();
  // Wait up to 5s for GPIO0 to go low to enter AP/setup mode.
  displayBusy(0);
  while (millis() < 5000) {
    if (digitalRead(SETUP_PIN) == 0 || !settings.ssid.length()) {
      stopDisplayBusy();
      return setupAP();
    }
    delay(50);
  }
  stopDisplayBusy();
  setupSTA();
}

void handleForm() {
  String update_wifi = server.arg("update_wifi");
  String t_ssid = server.arg("ssid");
  String t_psk = server.arg("psk");
  String t_timeserver = server.arg("ntpsrv");
  t_timeserver.toCharArray(settings.timeserver, EEPROM_TIMESERVER_LENGTH, 0);
  if (update_wifi == "1") {
    settings.ssid = t_ssid;
    settings.psk = t_psk;
  }
  String tz = server.arg("timezone");

  if (tz.length()) {
    settings.timezone = tz.toInt();
  }
  
  time_t newTime = getNtpTime();
  if (newTime) {
    setTime(newTime);
  }
  String syncInt = server.arg("ntpint");
  settings.interval = syncInt.toInt();

  settings.name = server.arg("clockname");
  settings.name.replace("+", " ");

  httpUpdateResponse = "The configuration was updated.";

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Moved");

  settings.Save();
  if (update_wifi == "1") {
    delay(500);
    setupWiFi();
  }
}

Ticker clock_ticker;

void setup() {
  setupDisplay();
  pinMode(SETUP_PIN, INPUT_PULLUP);
  setupWiFi();
  setupTime();
  server.on("/", handleRoot);
  server.on("/form", handleForm);
  server.begin();
  clock_ticker.attach_ms(TICK_MS, displayClock);
}

static void
open_space()
{
  if (space_open)
    return;
  space_open = true;
}

static void
close_space()
{
  if (!space_open)
    return;
  space_open = false;
  clearDigits();
  display();
}

static void
check_open()
{
  static uint32_t next_tick;
  int32_t dt;
  HTTPClient http;
  int status;

  if (next_tick == 0)
    next_tick = millis();
  dt = millis() - next_tick;
  if (dt < 0)
    return;
  next_tick = millis() + OPEN_POLL_SECONDS * 1000;

  http.begin("http://leedshackspace.org.uk/status.php");
  status = http.GET();
  if (status > 0) {
      String payload = http.getString();
      if (payload.indexOf("\"open\":true") >= 0)
	open_space();
      else
	close_space();
  }
  http.end();
}

void loop() {
  int32_t dt;
  time_t current_time;

  server.handleClient();
  if (clockMode == MODE_CLOCK && timeStatus() != timeNotSet) {
      current_time = now();
      dt = current_time - displayed_time;
      if (dt > JUMP_SECONDS || dt < -JUMP_SECONDS) {
	displayed_time = current_time;
	randomSeed(displayed_time);
      } else {
	  skew = dt;
      }
  }
  check_open();
}


unsigned int localPort = 4097;
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
byte sendBuffer[] = {
  0b11100011,          // LI, Version, Mode.
  0x0,                 // Stratum unspecified.
  0x6,                 // Polling interval
  0xEC,                // Clock precision.
  0x0, 0x0, 0x0, 0x0}; // Reference ...

void sendNTPpacket(WiFiUDP *u) {
  // Zeroise the buffer.
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  memcpy(packetBuffer, sendBuffer, 16);

  if (u->beginPacket(settings.timeserver, 123)) {
    u->write(packetBuffer, NTP_PACKET_SIZE);
    u->endPacket();
  }
}

time_t getNtpTime()
{
  WiFiUDP udp;
  udp.begin(localPort);
  while (udp.parsePacket() > 0) ; // discard any previously received packets
  for (int i = 0 ; i < 5 ; i++) { // 5 retries.
    sendNTPpacket(&udp);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
      if (udp.parsePacket()) {
         udp.read(packetBuffer, NTP_PACKET_SIZE);
         // Extract seconds portion.
         unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
         unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
         unsigned long secSince1900 = highWord << 16 | lowWord;
         udp.flush();
         return secSince1900 - 2208988800UL + settings.timezone * SECS_PER_HOUR;
      }
      delay(10);
    }
  }
  return 0; // return 0 if unable to get the time
}

void setupTime() {
  setSyncProvider(getNtpTime);
  setSyncInterval(settings.interval);
}


#if 0

// ======== Display constants.
#define _A_ 0x80
#define _B_ 0x40
#define _C_ 0x20
#define _D_ 0x10
#define _E_ 0x08
#define _F_ 0x04
#define _G_ 0x02
#define _DP_ 0x01

#else

//  8    A
// 7 2  F B
//  6    G
// 5 3  E C
//  4 1  D P

#define _A_ 0x01
#define _B_ 0x40
#define _C_ 0x20
#define _D_ 0x10
#define _E_ 0x08
#define _F_ 0x02
#define _G_ 0x04
#define _DP_ 0x80

#endif

#define DATA 0
#define CLOCK 1
#define BLANK 2

const char segments[] = {
  _A_|_B_|_C_|_D_|_E_|_F_,    // 0
  _B_|_C_,            // 1
  _A_|_B_|_G_|_D_|_E_,      // 2
  _A_|_B_|_C_|_D_|_G_,      // 3
  _B_|_C_|_F_|_G_,        // 4
  _A_|_C_|_D_|_F_|_G_,      // 5
  _A_|_C_|_D_|_E_|_F_|_G_,    // 6
  _A_|_B_|_C_,          // 7
  _A_|_B_|_C_|_D_|_E_|_F_|_G_,  // 8
  _A_|_B_|_C_|_D_|_F_|_G_,    // 9

  _A_|_B_|_C_|_E_|_F_|_G_, // A
  _C_|_D_|_E_|_F_|_G_, // b
  _D_|_E_|_G_, // c
  _B_|_C_|_D_|_E_|_G_, // d
  _A_|_D_|_E_|_F_|_G_, //e
  _A_|_E_|_F_|_G_, // f

  _A_|_B_|_E_|_F_|_G_, // P [0x10]

  _G_, // - [0x11]

  _A_, _B_, _C_, _D_, _E_, _F_, // 0x12 - 0x17

  0, // 0x18
};
  
char digits[6];
char decimals;

#define PULSE digitalWrite(CLOCK, LOW); delayMicroseconds(10) ; digitalWrite(CLOCK, HIGH); delayMicroseconds(10) ;

void display() {
  char i, n, d, digit;

  digitalWrite(BLANK, LOW);

  for (n = 0 ; n < 6 ; n++) {
    d = n;
    digit = segments[digits[d]];

    if ((decimals >> d & 0x1) == 0x1) digit |= _DP_;

    for (i = 0 ; i < 8 ; i++) {
      digitalWrite(DATA, digit & 0x1 ? HIGH : LOW);
      digit >>= 1;
      PULSE;
    }
  }
  digitalWrite(BLANK, HIGH);
}

void displayAP() {
  digits[3] = 0x10;
  digits[2] = 0xA;
  digits[1] = 0x18;
  digits[0] = 0x18;
  display();
}

void displayDash() {
  digits[0] = digits[1] = digits[2] = digits[3] = digits[4] = digits[5] = 0x11;
  display();
}

void clearDigits() {
  digits[0] = digits[1] = digits[2] = digits[3] = digits[4] = digits[5] = 0x18;
}

// Twirler handler.
Ticker ticker;

volatile char busySegment = 0x12;
volatile char busyDigit;

void _displayBusy() {
  if (busySegment > 0x17) {
    busySegment = 0x12;
  }
  clearDigits();
  digits[busyDigit] = busySegment++;
  display();
}

void displayBusy(char digit) {
  busyDigit = digit;
  ticker.attach(0.1, _displayBusy);
}

void stopDisplayBusy() {
  ticker.detach();
}

// End twirler handler.

// IP Display handler.
volatile signed char dispOctet = -1;

void _displayIP() {
  if (dispOctet > 3) {
    ticker.detach();
    dispOctet = -1;
    if (clockMode != MODE_CLOCK)
      displayAP();
    return;
  }
  clearDigits();
  uint8_t octet = (uint32_t(clockMode == MODE_CLOCK ? WiFi.localIP() : WiFi.softAPIP()) >> (8 * dispOctet++)) & 0xff;
  uint8_t d = 0;
  for (; octet > 99 ; octet -= 100) d++;
  digits[0] = d;
  d = 0;
  for (; octet > 9 ; octet -= 10) d++;
  digits[1] = d;
  digits[2] = octet;
  decimals = 0x1;
  display();
}

char displayIP() {
  if (dispOctet > -1) {
    return 1;
  }
  if (digitalRead(SETUP_PIN) == 1) return 0;
  dispOctet = 0;
  ticker.attach(1.0, _displayIP);
  return 1;
}

// end Ip display handler.

void to_digits(char *digits, uint8_t val)
{
  uint8_t high = 0;

  while (val >= 10) {
      high++;
      val -= 10;
  }
  digits[0] = high;
  digits[1] = val;
}

void displayClock() {
  static int next_tick;
  static uint32_t last_millis;
  uint32_t cur_millis;

  if (displayIP())
    return;
  if (clockMode != MODE_CLOCK)
    return;
  if (displayed_time == 0)
    return;

  cur_millis = millis();
  if (last_millis != 0) {
    next_tick -= (cur_millis - last_millis);
  }
  last_millis = cur_millis;

  if (next_tick > 0) {
    return;
  }

  next_tick += 500;
  if (good_ticks == 0) {
      next_tick += random(-JITTER, JITTER + 1);
      good_ticks = random(MAX_GOOD_TICKS + 1);
  } else {
      good_ticks--;
  }
  half = !half;
  if (half)
    displayed_time++;

  next_tick += skew;

  if (!space_open)
    return;

  TimeElements tm;
  breakTime(displayed_time, tm);
  int h = tm.Hour;
  int m = tm.Minute;
  int s = tm.Second;
  decimals = 0;

  to_digits(digits + 0, h);
  to_digits(digits + 2, m);
  to_digits(digits + 4, s);

  if (half) decimals = 0x0a;
  //if (timeStatus() != timeSet) decimals |= 0x20;
  display();
}

void setupDisplay() {
  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(BLANK, OUTPUT);
  displayDash();
}

