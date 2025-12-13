// LE 07/12/2025 à 16H, ce code fonctionne
// A TESTER SUR TOUTES LES HEURES 
// OBSERVER PARTICULIEREMENT LE DECHET SONORE POSSIBLE APRES L'ANNONCE DE L'HEURE
// ----------------------------------------------------
// Connection : carte ESP32C3 Dev Module PORT COM9

// includes & libs
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ==================== AJOUT DF1201S ====================
#include <DFRobot_DF1201S.h>
#define DF_RX 20
#define DF_TX 21
#define DF1201SSerial Serial1
DFRobot_DF1201S DF1201S;


// ----------------- Pinout -----------------
#define SDA_PIN 5
#define SCL_PIN 6
//#define LED_PIN 0
#define LED_PIN 8
#define NOMBRE_LEDS 56
#define BocheBuzzer 0

// ----------------- OLED (U8g2) -----------------
// Reset none, clk=SCL_PIN, data=SDA_PIN
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

// ----------------- SHT30 -----------------
Adafruit_SHT31 sht30 = Adafruit_SHT31();

// ----------------- NeoPixel -----------------
Adafruit_NeoPixel colonneLed = Adafruit_NeoPixel(NOMBRE_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ----------------- Buttons -----------------
const int BTN_UP   = 4;
const int BTN_NEXT = 7;
const int BTN_OK   = 10;

// ----------------- Wifi prefs -----------------
Preferences prefs;
String ssid = "";
String password = "";
bool inConfig = false;

// ----------------- CHAR selection for input -----------------
const char* CHAR_LINES[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  " !@#$_-.,:/+*()?=<"
};
const int NUM_LINES = 4;

// ----------------- Temperature scale -----------------
const float TEMP_MIN = 12.0;
const float TEMP_MAX = 39.5;
const float DEG_PER_LED = 0.5; // 0.5°C per LED
const float FACTEUR_LED = 1.0 / DEG_PER_LED; // conversion °C -> leds

// ----------------- Timing & behavior -----------------
const unsigned long INTERVAL_MS = 500UL; // read every 500 ms
const unsigned long FrequenceReleveVariations = 60000UL; // 60s
const unsigned long TempoAffichageVariations = 10000UL; // 10s animation trend
const unsigned long OLED_ALTERNATE_MS = 5000UL; // alternate OLED view every 5s

// ----------------- Measurement vars -----------------
float temperatureCourante = NAN;
float T1 = 0.0;
float T2 = 0.0;
unsigned long t1TempoReleveVariations = 0;
bool MajAffichageVariations = false;

// ----------------- NTP -----------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ============== AJOUT : variables DFPlayer / annonce ==========
static unsigned long lastAnnounceCheck = 0;
static int derniereHeureAnnonce = -1;

// Volumes par fichier hourIndex 1..24 (tel que dans ton code d'essai)
const uint8_t volumeTable[25] = {
  0,  30,26,26,22,26,28,26,22,26,26,26,26,26,30,26,28,26,26,28,24,28,28,26,30
};
// ==============================================================

// ----------------- OLED helpers -----------------
void afficheTemperatureOLED(float temp) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_fub20_tf);
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f\u00B0", temp); // e.g. 23.9°
  int16_t w = u8g2.getUTF8Width(buf);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, 28, buf);
  u8g2.sendBuffer();
}

/* ---------- CORRIGE : getLocalTimeString() (inchangé) ---------- */
String getLocalTimeString() {
  timeClient.update();
  unsigned long epoch = timeClient.getEpochTime(); // UTC epoch

  // 1) Compute local standard time = UTC + 1h (CET, without DST)
  time_t localStandard = (time_t)(epoch + 3600); // CET base offset
  struct tm ls;
  gmtime_r(&localStandard, &ls); // thread-safe conversion

  int year = ls.tm_year + 1900;
  int month = ls.tm_mon + 1;   // 1..12
  int mday = ls.tm_mday;       // 1..31
  int wday = ls.tm_wday;       // 0=Sun..6=Sat
  int hour = ls.tm_hour;       // 0..23

  // Find last Sunday of the month (local standard)
  // Determine last day of month:
  int lastDay;
  if (month == 1 || month == 3 || month == 5 || month == 7 || month == 8 || month == 10 || month == 12) lastDay = 31;
  else if (month == 4 || month == 6 || month == 9 || month == 11) lastDay = 30;
  else {
    // february -> check leap year
    bool leap = ( (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0) );
    lastDay = leap ? 29 : 28;
  }

  // compute weekday of last day: create tm for lastDay at noon to be safe
  struct tm tmp = ls;
  tmp.tm_mday = lastDay;
  tmp.tm_hour = 12; tmp.tm_min = 0; tmp.tm_sec = 0;
  mktime(&tmp); // normalize and fill tm_wday
  int wdayLast = tmp.tm_wday; // 0..6

  int lastSunday = lastDay - wdayLast; // day of month that is last sunday

  // Default: not DST
  bool isDST = false;

  if (month < 3 || month > 10) {
    isDST = false;
  } else if (month > 3 && month < 10) {
    isDST = true;
  } else if (month == 3) {
    // if after last Sunday -> DST
    if (mday > lastSunday) isDST = true;
    else if (mday < lastSunday) isDST = false;
    else { // mday == lastSunday -> DST starts at 02:00 local (CET -> CEST)
      if (hour >= 2) isDST = true;
      else isDST = false;
    }
  } else if (month == 10) {
    // if before last Sunday -> DST
    if (mday < lastSunday) isDST = true;
    else if (mday > lastSunday) isDST = false;
    else { // mday == lastSunday -> DST ends at 03:00 local (CEST -> CET)
      if (hour < 3) isDST = true;
      else isDST = false;
    }
  }

  // Now compute final local epoch: CET (UTC+1) + (DST? +1h)
  long offset = 3600 + (isDST ? 3600 : 0);
  time_t local = (time_t)(epoch + offset);
  struct tm lt;
  gmtime_r(&local, &lt);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);

  // (Optional) debug on Serial to trace DST decisions
  Serial.print("UTC epoch: "); Serial.print(epoch);
  Serial.print(" | local std: "); Serial.print(localStandard);
  Serial.print(" | month: "); Serial.print(month);
  Serial.print(" mday: "); Serial.print(mday);
  Serial.print(" lastSun: "); Serial.print(lastSunday);
  Serial.print(" hour: "); Serial.print(hour);
  Serial.print(" isDST: "); Serial.println(isDST ? "Y" : "N");

  return String(buf);
}

// ----------------- NeoPixel utils -----------------
void FonctionEffaceBarrettesLeds() {
  colonneLed.clear();
  colonneLed.show();
}

// Affiche température sur leds (bargraph)
void AfficheTemperatureSurLeds(float temp) {
  if (isnan(temp)) return;
  if (temp < TEMP_MIN) temp = TEMP_MIN;
  if (temp > TEMP_MAX) temp = TEMP_MAX;

  float ledsFloat = (temp - TEMP_MIN) / DEG_PER_LED;
  int ledsOn = (int)floor(ledsFloat + 0.0001);

  for (int i = 0; i < NOMBRE_LEDS; i++) {
    if (i < ledsOn) {
      // rouge normale (doux)
      colonneLed.setPixelColor(i, colonneLed.Color(40, 0, 0));
    } else if (i == ledsOn) {
      // dernière led plus brillante (accent)
      colonneLed.setPixelColor(i, colonneLed.Color(255, 0, 255));
    } else {
      // au-dessus = vert sombre
      colonneLed.setPixelColor(i, colonneLed.Color(0, 40, 0));
    }
  }
  colonneLed.show();
}

// Helper : retourne la seconde actuelle 0..59
// Si WiFi disponible, on récupère via getLocalTimeString() (NTP) ; sinon fallback sur millis()
int getSecondsNow() {
  if (WiFi.status() == WL_CONNECTED) {
    String hhmmss = getLocalTimeString(); // function calls timeClient.update()
    if (hhmmss.length() >= 8) {
      return hhmmss.substring(6,8).toInt();
    } else {
      // fallback in case getLocalTimeString returned odd value
      return (millis() / 1000) % 60;
    }
  } else {
    return (millis() / 1000) % 60; // fallback when no NTP
  }
}

// Is in critical zone (return true if we are in window where we must avoid starting a blocking trend)
// default window: seconds >= 50 OR seconds <= 10
bool isInCriticalZone() {
  int s = getSecondsNow();
  //if (s >= 50 || s <= 10) return true;

  if (s >= 30 || s <= 20) return true;

  return false;
}

// Animation de la vitesse de variation (température uniquement)
// NOTE : on laisse la fonction bloquante comme tu l'avais, mais on n'appelle la fonction
//       que si isInCriticalZone() == false. Si l'animation a déjà démarré, l'appel reste bloquant.
void FonctionVariationTemperature(float A, float B) {
  float delta = B - A; // °C
  int nbLeds = (int)round(delta * FACTEUR_LED);
  if (nbLeds > NOMBRE_LEDS) nbLeds = NOMBRE_LEDS;
  if (nbLeds < -NOMBRE_LEDS) nbLeds = -NOMBRE_LEDS;

  unsigned long tDebut = millis();

  if (nbLeds == 0) {
    while (millis() - tDebut < TempoAffichageVariations) {
      for (int i = 27, j = 28; i >= 0 && j < NOMBRE_LEDS; i--, j++) {
        colonneLed.setPixelColor(27, colonneLed.Color(255, 255, 0)); // centre jaune
        colonneLed.setPixelColor(i, colonneLed.Color(0, 0, 150));    // bleu bas
        colonneLed.setPixelColor(j, colonneLed.Color(120, 0, 0));    // rouge haut (un peu plus doux)
        colonneLed.show();
        delay(25);
      }
      FonctionEffaceBarrettesLeds();
    }
    return;
  }

  if (nbLeds > 0) {
    while (millis() - tDebut < TempoAffichageVariations) {
      for (int i = 0; i < nbLeds; i++) {
        colonneLed.setPixelColor(i, colonneLed.Color(150, 0, 0));
        colonneLed.show();
        delay(25);
      }
      FonctionEffaceBarrettesLeds();
    }
    return;
  }

  // nbLeds < 0 (descente)
  int absL = abs(nbLeds);
  while (millis() - tDebut < TempoAffichageVariations) {
    for (int i = NOMBRE_LEDS - 1; i >= NOMBRE_LEDS - absL; i--) {
      colonneLed.setPixelColor(i, colonneLed.Color(0, 0, 150));
      colonneLed.show();
      delay(25);
    }
    FonctionEffaceBarrettesLeds();
  }
}

// ----------------- WiFi / input helpers -----------------
bool readBtn(int pin) { return digitalRead(pin) == LOW; }
bool waitRelease(int pin) { while(readBtn(pin)) delay(10); return true; }

void drawCenteredText(const char* txt, int y) {
  int16_t w = u8g2.getUTF8Width(txt);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, y, txt);
}

String maskedIP(IPAddress ip){
  return String(ip[0]) + "." + String(ip[1]) + ".xx.xx";
}

// OLED input screen for config (unchanged)
String tempInput = "";
int lineIndex = 0;
int charIndex = 0;

void showOLEDInput() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  const int maxVisible = 12;
  String visibleInput;
  if (tempInput.length() <= maxVisible) visibleInput = tempInput + "_";
  else visibleInput = tempInput.substring(tempInput.length() - maxVisible) + "_";
  u8g2.drawUTF8(0, 10, visibleInput.c_str());
  char buf[32];
  const char *typeStr = (lineIndex==0)?"Min":(lineIndex==1)?"Maj":(lineIndex==2)?"Num":"Sym";
  char cur = CHAR_LINES[lineIndex][charIndex % strlen(CHAR_LINES[lineIndex])];
  snprintf(buf, sizeof(buf), "T:%s C:%c", typeStr, cur);
  u8g2.drawUTF8(0, 25, buf);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawUTF8(0, 38, "UP NEXT OK");
  u8g2.sendBuffer();
}

void enterConfigMode() {
  tempInput = "";
  lineIndex = 0;
  charIndex = 0;
  ssid = "";
  password = "";

  // SSID
  while(ssid == "") {
    showOLEDInput();
    if (readBtn(BTN_UP)) { charIndex++; if (charIndex >= (int)strlen(CHAR_LINES[lineIndex])) charIndex = 0; waitRelease(BTN_UP); }
    if (readBtn(BTN_NEXT)) { lineIndex++; if (lineIndex >= NUM_LINES) lineIndex = 0; charIndex = 0; waitRelease(BTN_NEXT); }
    if (readBtn(BTN_OK)) {
      unsigned long start = millis();
      while (readBtn(BTN_OK)); // wait release
      unsigned long dur = millis() - start;
      if (dur > 1000) { ssid = tempInput; tempInput = ""; break; }
      else {
        char c = CHAR_LINES[lineIndex][charIndex];
        if (c == '<') { if (tempInput.length() > 0) tempInput.remove(tempInput.length()-1); }
        else tempInput += c;
      }
    }
    delay(10);
  }

  // Password
  while(password == "") {
    showOLEDInput();
    if (readBtn(BTN_UP)) { charIndex++; if (charIndex >= (int)strlen(CHAR_LINES[lineIndex])) charIndex = 0; waitRelease(BTN_UP); }
    if (readBtn(BTN_NEXT)) { lineIndex++; if (lineIndex >= NUM_LINES) lineIndex = 0; charIndex = 0; waitRelease(BTN_NEXT); }
    if (readBtn(BTN_OK)) {
      unsigned long start = millis();
      while (readBtn(BTN_OK));
      unsigned long dur = millis() - start;
      if (dur > 1000) { password = tempInput; tempInput = ""; break; }
      else { char c = CHAR_LINES[lineIndex][charIndex]; if (c == '<') { if (tempInput.length()>0) tempInput.remove(tempInput.length()-1); } else tempInput += c; }
    }
    delay(10);
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);

  u8g2.clearBuffer();
  drawCenteredText("Identifiants sauvegardes !", 20);
  u8g2.sendBuffer();
  delay(1000);
}

// show wifi status (unchanged)
void showWiFiStatus() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (WiFi.status() == WL_CONNECTED) {
    drawCenteredText("GOOD !",12);
    String ip = maskedIP(WiFi.localIP());
    u8g2.setFont(u8g2_font_6x10_tr);
    int16_t w = u8g2.getUTF8Width(ip.c_str());
    int x = (72 - w) / 2; if (x < 0) x = 0;
    u8g2.drawUTF8(x, 30, ip.c_str());
  } else {
    drawCenteredText("NO GOOD !",24);
  }
  u8g2.sendBuffer();
}

bool connectWiFi(unsigned long timeout=30000) {
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

// show long press invitation and detect appui long to enter config
void showAppuiLongInvitation(int durationSec) {
  unsigned long startTime = millis();
  const int barX = 4;
  const int fullW = 64;
  const int barY = 24;
  while (millis() - startTime < (unsigned long)durationSec * 1000) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    if (((millis() / 500) % 2) == 0) drawCenteredText("Appui long...", 10);
    unsigned long elapsed = millis() - startTime;
    int curW = map(elapsed, 0, durationSec * 1000, 0, fullW);
    u8g2.drawFrame(barX, barY, fullW, 6);
    if (curW > 0) u8g2.drawBox(barX, barY, curW, 6);
    int secondsLeft = durationSec - (elapsed / 1000);
    char ssec[8];
    snprintf(ssec, sizeof(ssec), "%2ds", secondsLeft);
    drawCenteredText(ssec, barY + 14);
    u8g2.sendBuffer();
    delay(50);

    if (readBtn(BTN_OK)) {
      unsigned long ps = millis();
      while (readBtn(BTN_OK));
      if (millis() - ps > 2000) {
        inConfig = true;
        prefs.clear();
        ssid = "";
        password = "";
        enterConfigMode();
        break;
      }
    }
  }
}

// =================== AJOUT : bipBuzzer + annonceHeure =====================
void bipBuzzer (int TempsH, int TempsL, int nb) {
  pinMode(BocheBuzzer, OUTPUT);
  digitalWrite(BocheBuzzer, LOW);
  for (int x = 1; x <= nb; x++) {
    digitalWrite(BocheBuzzer, HIGH);
    delay (TempsH);
    digitalWrite(BocheBuzzer, LOW);
    delay (TempsL);
  }
}



/*void annonceHeure(int heure) {
  int index = (heure == 0) ? 24 : heure; // 0 -> 24

  // Volume depuis la table
  uint8_t vol = 26;
  if (index >= 1 && index <= 24) vol = volumeTable[index];

  // Beep 3 fois
  bipBuzzer(70, 70, 3);

  // Démarre DF1201S
  DF1201S.start();
  delay(50);

  // Volume adapté
  DF1201S.setVol(vol);

  // Mode SINGLE
  //DF1201S.setPlayMode(DF1201S.SINGLE);
//DF1201S.setPlayMode(SINGLE);
DF1201S.setPlayMode(DF1201S.SINGLE);
  // Lecture du fichier numéro index
  DF1201S.playFileNum(index);

  // laisser le DFPlayer finir son fichier
  //delay(3000);
  //delay(2500);
  delay(2800);

  // Pause (comme ton code précédent)
  DF1201S.pause();
}*/

//+++++++++++++++++++

void annonceHeure(int heure) {
  // 1) Convertit 0 → 24 pour le fichier
  int index = (heure == 0) ? 24 : heure;

  // 2) Construit le chemin complet du fichier MP3
  char fichier[32];
  snprintf(fichier, sizeof(fichier), "/wife/%03dF.mp3", index);

  // 3) Volume personnalisé (table déjà existante)
  uint8_t vol = 26;
  if (index >= 1 && index <= 24) vol = volumeTable[index];

  // 4) Petit bip avant l'annonce
  bipBuzzer(70, 70, 3);

  // 5) Prépare DFPlayer
  //DF1201S.start();
  delay(50);
  DF1201S.setVol(vol);
  DF1201S.setPlayMode(DF1201S.SINGLE);   // important : lecture unique

  // 6) Lecture du fichier exact par chemin
  DF1201S.playSpecFile(String(fichier));

  // 7) Délai d’attente en fonction de la durée (simple version)
  //delay(2200);    // on ajustera demain si besoin
  //delay(1500);
  //delay(1000);
  //delay(0);

  // 8) Stop propre
  //DF1201S.pause();
}


//+++++++++++++++++++

// ============================================================================

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();

  colonneLed.begin();
  colonneLed.setBrightness(80);
  FonctionEffaceBarrettesLeds();

  // SHT30 init forced at 0x45
  bool sht_ok = sht30.begin(0x45);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  if (!sht_ok) {
    u8g2.drawUTF8(0, 10, "SHT30 non detecte !");
    u8g2.sendBuffer();
    Serial.println("Erreur: SHT30 non detecte sur 0x45");
  } else {
    u8g2.drawUTF8(0, 10, "SHT30 init OK");
    u8g2.sendBuffer();
  }

  prefs.begin("wifi", false);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");

  // === AJOUT DF1201S INIT ===
  DF1201SSerial.begin(115200, SERIAL_8N1, DF_RX, DF_TX);
  // tente connexion au DFPlayer PRO (bloquant court)

  DF1201S.setPlayMode(DF1201S.SINGLE);

  if (!DF1201S.begin(DF1201SSerial)) {
    Serial.println("DF1201S non detecte !");
    // on continue sans son si absent
  } else {
    Serial.println("DF1201S OK !");
    DF1201S.switchFunction(DF1201S.MUSIC);
    DF1201S.setPrompt(false);
    DF1201S.setVol(26);
    delay(200);
  }
  // ============================================================

  // show long press invitation
  showAppuiLongInvitation(12);

  bool wifiOK = false;
  if (ssid != "" && password != "") wifiOK = connectWiFi(30000);
  showWiFiStatus();

  if (wifiOK) {
    timeClient.begin();
    timeClient.setUpdateInterval(60000); // 60 secondes
    delay(2000);
    // display time once
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    String ts = getLocalTimeString();
    int16_t w = u8g2.getUTF8Width(ts.c_str()); int x = (72 - w)/2; if (x < 0) x = 0;
    u8g2.drawUTF8(x, 32, ts.c_str()); u8g2.sendBuffer();
  }

  // initial OLED show temp 0.0
  afficheTemperatureOLED(0.0);
}

// ----------------- Loop -----------------
void loop() {
  static unsigned long lastRead = 0;
  static unsigned long lastOledSwitch = 0;
  static bool oledShowTime = false;
  // static unsigned long lastAnnounceCheck = 0; // supprimé : on utilise la globale déclarée plus haut
  unsigned long now = millis();

  // Read sensor every INTERVAL_MS
  if (now - lastRead >= INTERVAL_MS) {
    lastRead = now;
    float t = sht30.readTemperature();
    if (!isnan(t)) {
      temperatureCourante = t;
    } else {
      Serial.println("Lecture SHT30 echouee");
    }

    // update OLED (either time or temp depending on alternation and WiFi)
    if (inConfig) {
      // config UI handled in enterConfigMode (blocking)
    } else {
      if (WiFi.status() == WL_CONNECTED) {
        // alternate every OLED_ALTERNATE_MS
        if (now - lastOledSwitch >= OLED_ALTERNATE_MS) {
          oledShowTime = !oledShowTime;
          lastOledSwitch = now;
        }
        if (oledShowTime) {
          String ts = getLocalTimeString();
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_7x14B_tr);
          int16_t w = u8g2.getUTF8Width(ts.c_str()); int x = (72 - w)/2; if (x < 0) x = 0;
          u8g2.drawUTF8(x, 32, ts.c_str());
          u8g2.sendBuffer();
        } else {
          if (!isnan(temperatureCourante)) afficheTemperatureOLED(temperatureCourante);
        }
      } else {
        // no wifi: show temperature (or error)
        if (!isnan(temperatureCourante)) afficheTemperatureOLED(temperatureCourante);
        else {
          u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tr);
          u8g2.drawUTF8(0, 12, "Err SHT30");
          u8g2.sendBuffer();
        }
      }
    }

    // update leds bargraph
    if (!isnan(temperatureCourante)) AfficheTemperatureSurLeds(temperatureCourante);
  }

  // Tendance sampling management (inchangé)
  if (t1TempoReleveVariations == 0 && !isnan(temperatureCourante)) {
    T1 = temperatureCourante;
    t1TempoReleveVariations = millis();
  } else if (t1TempoReleveVariations != 0 && (millis() - t1TempoReleveVariations >= FrequenceReleveVariations)) {
    if (!isnan(temperatureCourante)) {
      T2 = temperatureCourante;
      MajAffichageVariations = true;
    }
    t1TempoReleveVariations = 0;
  }

  // run trend animation if requested (priority) BUT avoid the critical zone
  if (MajAffichageVariations) {
    if (isInCriticalZone()) {
      // On reporte l'animation : on n'appelle pas FonctionVariationTemperature maintenant
      // MajAffichageVariations reste true et sera retenté plus tard (hors zone critique)
      // On peut aussi ajouter ici un petit log pour debug
      // Serial.println("Trend deferred due to critical zone");
    } else {
      // Run animation (this blocks for its duration as in original)
      FonctionVariationTemperature(T1, T2);
      MajAffichageVariations = false;
      // prepare next window
      T1 = T2;
      t1TempoReleveVariations = 0;
      // after trend animation, restore the OLED immediate display and leds
      if (!isnan(temperatureCourante)) {
        AfficheTemperatureSurLeds(temperatureCourante);
        if (WiFi.status() == WL_CONNECTED) afficheTemperatureOLED(temperatureCourante); // temporary show temp then alternation continues
      }
    }
  }

  // -------------------- ANNOUNCE HOUR (DF1201S) --------------------
  if (now - lastAnnounceCheck >= 1000) {
    lastAnnounceCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      String hhmmss = getLocalTimeString(); // calls timeClient.update() internally
      int h = hhmmss.substring(0,2).toInt();
      int m = hhmmss.substring(3,5).toInt();
      int s = hhmmss.substring(6,8).toInt();

      //if (m == 0 && s < 3) { // during first 3 seconds of hour

      if (m == 0 && s == 0) { // during first 3 seconds of hour
        if (derniereHeureAnnonce != h) {
          annonceHeure(h);
          derniereHeureAnnonce = h;
        }
      } else {
        // réarmement : quand on quitte la minute 00, on autorise la prochaine annonce
        if (m != 0) {
          derniereHeureAnnonce = -1;
        }
      }
    }
  }
  // ------------------------------------------------------------------

  // Button: check appui long on OK during normal run to force entering config
  static bool okbuttonPressed = false;
  static unsigned long okpressStart = 0;
  if (readBtn(BTN_OK)) {
    if (!okbuttonPressed) okpressStart = millis();
    okbuttonPressed = true;
    if (millis() - okpressStart > 2000 && !inConfig) {
      // enter config mode
      inConfig = true;
      prefs.clear();
      ssid = ""; password = "";
      enterConfigMode();
      // after entering, try to connect wifi
      if (ssid != "" && password != "") {
        bool wifiOK = connectWiFi(30000);
        if (wifiOK) {
          timeClient.begin(); timeClient.update();
        }
      }
      inConfig = false;
    }
  } else {
    okbuttonPressed = false;
  }

  delay(10);
}
