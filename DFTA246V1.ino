// LE 13/12/2025
// DFTA246
// Version V1 pas d'évenement uniquement heure température oled et bargraph plus annonce vocale heure
// Vidéo YouTube : https://youtu.be/PGV0VyRELZA
// GitHub de ce projet : https://github.com/claudiusmarius/ESP32-THERMOMETRE-A-LEDS-WS2812B-AVEC-ANNONCE-VOCALE-HEURE-DFTA246
// Mon YouTube général : https://www.youtube.com/c/ClaudeDufourmont
// Mon GitHub général : https://github.com/claudiusmarius
// ----------------------------------------------------
// Connection : carte ESP32C3 Dev Module PORT COM8 (ou autre)

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

// ----------------- Bouttons -----------------
const int BTN_UP   = 4;
const int BTN_NEXT = 7;
const int BTN_OK   = 10;

// ----------------- Wifi prefs -----------------
Preferences prefs;
String ssid = "";
String password = "";
bool inConfig = false;

// ----------------- Sélection de caractères pour l'entrée -----------------
const char* CHAR_LINES[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  " !@#$_-.,:/+*()?=<"
};
const int NUM_LINES = 4;

// ----------------- Echelle de température -----------------
const float TEMP_MIN = 12.0;
const float TEMP_MAX = 39.5;
const float DEG_PER_LED = 0.5; // 0.5°C par LED
const float FACTEUR_LED = 1.0 / DEG_PER_LED; // conversion °C -> leds

// ----------------- // Temporisation et comportement -----------------
const unsigned long INTERVAL_MS = 500UL; // lecture toutes les 500 ms
const unsigned long FrequenceReleveVariations = 60000UL; // 60s
const unsigned long TempoAffichageVariations = 10000UL; // 10s animation tendance
const unsigned long OLED_ALTERNATE_MS = 5000UL; // alternance d'affichage OLED toutes les 5 secondes

// ----------------- Variables de mesure -----------------
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

// Volumes par fichier hourIndex 1..24
const uint8_t volumeTable[25] = {
  0,  30,26,26,22,26,28,26,22,26,26,26,26,26,30,26,28,26,26,28,24,28,28,26,30
};
// ==============================================================

// ----------------- Assistants OLED -----------------
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

/* ---------------------------------------------------------------------------
   getLocalTimeString()

   Rôle :
   - Récupère l’heure UTC depuis le client NTP
   - Calcule l’heure locale française (CET / CEST)
   - Gère automatiquement le changement d’heure été / hiver
   - Retourne une chaîne au format "HH:MM:SS"

   Remarques :
   - Ne bloque pas la loop()
   - Ne dépend pas d’un RTC matériel
   - La logique DST est volontairement écrite “à la main” pour être maîtrisée
--------------------------------------------------------------------------- */

String getLocalTimeString() {
  // Met à jour le client NTP (si nécessaire)
  timeClient.update();

  // Récupération du temps UTC sous forme d’epoch (secondes depuis 01/01/1970)
  unsigned long epoch = timeClient.getEpochTime();        // UTC epoch

  // ------------------------------------------------------------------------
  // 1) Calcul de l’heure locale standard (CET = UTC + 1h, sans heure d’été)
  // ------------------------------------------------------------------------
  
  time_t localStandard = (time_t)(epoch + 3600);          // Décalage UTC → CET
  struct tm ls;
  gmtime_r(&localStandard, &ls);                          // Conversion epoch → date/heure

  // Extraction des éléments de date nécessaires au calcul DST
  int year = ls.tm_year + 1900;
  int month = ls.tm_mon + 1;   // 1..12
  int mday = ls.tm_mday;       // 1..31
  int wday = ls.tm_wday;       // 0=Sun..6=Sat
  int hour = ls.tm_hour;       // 0..23

  // ------------------------------------------------------------------------
  // 2) Recherche du dernier dimanche du mois courant
  //    (règle officielle pour le changement d’heure en Europe)
  // ------------------------------------------------------------------------
  
  int lastDay;
  // Détermination du dernier jour du mois
  if (month == 1 || month == 3 || month == 5 || month == 7 || month == 8 || month == 10 || month == 12) lastDay = 31;
  else if (month == 4 || month == 6 || month == 9 || month == 11) lastDay = 30;
  else {
    // february -> check leap year
    bool leap = ( (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0) );
    lastDay = leap ? 29 : 28;
  }

  // Création d’une date temporaire positionnée au dernier jour du mois à midi
  // (midi évite les problèmes de bascule d’heure)
  struct tm tmp = ls;
  tmp.tm_mday = lastDay;
  tmp.tm_hour = 12; tmp.tm_min = 0; tmp.tm_sec = 0;

  // Normalisation de la structure (calcule tm_wday automatiquement)
  mktime(&tmp); // normaliser et remplir tm_wday
  
  // Jour de la semaine du dernier jour du mois (0=dimanche)
  int wdayLast = tmp.tm_wday; // 0..6

  int lastSunday = lastDay - wdayLast; // jour du mois qui est le dernier dimanche

  // ------------------------------------------------------------------------
  // 3) Détermination de l’heure d’été (DST) ou non
  // ------------------------------------------------------------------------
  
  bool isDST = false;                                               // Par défaut : heure d’hiver

  if (month < 3 || month > 10) {
    // Janvier, février, novembre, décembre → heure d’hiver
    isDST = false;
  } else if (month > 3 && month < 10) {
    // Avril à septembre → heure d’été
    isDST = true;
  } else if (month == 3) {
    // Passage à l’heure d’été : dernier dimanche de mars à 02:00
    if (mday > lastSunday) isDST = true;
    else if (mday < lastSunday) isDST = false;
    else {
       // Jour exact du changement
      if (hour >= 2) isDST = true;
      else isDST = false;
    }
  } else if (month == 10) {
    // Retour à l’heure d’hiver : dernier dimanche d’octobre à 03:00
    if (mday < lastSunday) isDST = true;
    else if (mday > lastSunday) isDST = false;
    else {
       // Jour exact du changement
      if (hour < 3) isDST = true;
      else isDST = false;
    }
  }

  // ------------------------------------------------------------------------
  // 4) Calcul final de l’heure locale
  // ------------------------------------------------------------------------

  // CET = UTC +1h, CEST = UTC +2h
  long offset = 3600 + (isDST ? 3600 : 0);
  time_t local = (time_t)(epoch + offset);
  struct tm lt;
  gmtime_r(&local, &lt);

  // Mise en forme de la chaîne HH:MM:SS
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);

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
    String hhmmss = getLocalTimeString(); // fonction calls timeClient.update()
    if (hhmmss.length() >= 8) {
      return hhmmss.substring(6,8).toInt();
    } else {
      // solution de repli si getLocalTimeString renvoie une valeur impaire
      return (millis() / 1000) % 60;
    }
  } else {
    return (millis() / 1000) % 60; // Solution de repli en l'absence de NTP
  }
}

// Se trouve dans une zone critique (renvoie vrai si nous sommes dans une fenêtre où nous devons éviter de déclencher une tendance bloquante)
// fenêtre par défaut : secondes >= 50 OU secondes <= 10
bool isInCriticalZone() {
  int s = getSecondsNow();
  //if (s >= 50 || s <= 10) return true;

  if (s >= 30 || s <= 20) return true;

  return false;
}

// Animation de la vitesse de variation (température uniquement)
// NOTE : on laisse la fonction bloquante, mais on n'appelle la fonction
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

// ----------------- Assistants Wi-Fi / saisie -----------------
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

// OLED input
String tempInput = "";
int lineIndex = 0;
int charIndex = 0;

/* ---------------------------------------------------------------------------
   showOLEDInput()

   Rôle :
   - Affiche sur l’écran OLED la saisie utilisateur (ex : mot de passe WiFi)
   - Gère le défilement horizontal si le texte est trop long
   - Affiche le type de caractères sélectionnés (Min, Maj, Num, Sym)
   - Affiche le caractère actuellement sélectionné
   - Affiche l’aide des boutons (UP / NEXT / OK)

   Contexte :
   - Utilisée lors de la saisie manuelle des identifiants WiFi
   - Fonction purement graphique (aucune logique réseau ici)
--------------------------------------------------------------------------- */
void showOLEDInput() {

  // Efface le buffer graphique (rien n’est encore envoyé à l’écran)
  u8g2.clearBuffer();

  // Police principale pour la saisie
  u8g2.setFont(u8g2_font_6x10_tr);

  // Nombre maximal de caractères visibles sur une ligne
  const int maxVisible = 12;

  String visibleInput;

  // Si la saisie tient sur l’écran, on l’affiche entièrement
  // Sinon, on n’affiche que les derniers caractères (effet “scroll”)
  if (tempInput.length() <= maxVisible) {
    visibleInput = tempInput + "_";   // Curseur visuel
  } else {
    visibleInput = tempInput.substring(tempInput.length() - maxVisible) + "_";
  }

  // Affichage de la chaîne saisie
  u8g2.drawUTF8(0, 10, visibleInput.c_str());

  // ------------------------------------------------------------------------
  // Affichage du type de caractères et du caractère courant
  // ------------------------------------------------------------------------

  char buf[32];

  // Détermination du type de caractères sélectionné
  // lineIndex : 0 = minuscules, 1 = majuscules, 2 = chiffres, 3 = symboles
  const char *typeStr =
      (lineIndex == 0) ? "Min" :
      (lineIndex == 1) ? "Maj" :
      (lineIndex == 2) ? "Num" : "Sym";

  // Récupération du caractère actuellement sélectionné
  char cur = CHAR_LINES[lineIndex][charIndex % strlen(CHAR_LINES[lineIndex])];

  // ------------------------------------------------------------------------
  // Correction d’un ancien bug d’affichage
  //
  // Ancien affichage : "T:Sym C:#"
  // → le "T:" était un reliquat d’un ancien mot ("Type")
  // → absence d’espace rendait l’affichage confus
  //
  // Nouvel affichage clair :
  // "Sym : #"
  // ------------------------------------------------------------------------

  snprintf(buf, sizeof(buf), "%s : %c", typeStr, cur);
  u8g2.drawUTF8(0, 25, buf);

  // ------------------------------------------------------------------------
  // Affichage de l’aide utilisateur (fonctions des boutons)
  // ------------------------------------------------------------------------

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawUTF8(0, 38, "UP NEXT OK");

  // Envoi du buffer vers l’écran OLED
  u8g2.sendBuffer();
}

/* ---------------------------------------------------------------------------
   enterConfigMode()

   Rôle :
   - Permet la saisie manuelle des identifiants WiFi (SSID + mot de passe)
   - Utilise uniquement 3 boutons : UP / NEXT / OK
   - Affiche en temps réel la saisie sur l’écran OLED
   - Sauvegarde les identifiants en mémoire non volatile (NVS / Preferences)

   Principe de navigation :
   - UP    : change le caractère courant
   - NEXT  : change la famille de caractères (Min, Maj, Num, Sym)
   - OK court  : ajoute le caractère sélectionné (ou efface si '<')
   - OK long (>1s) : valide la saisie en cours

   Fonction bloquante assumée :
   - Le système reste dans ce mode tant que la saisie n’est pas validée
--------------------------------------------------------------------------- */
void enterConfigMode() {

  // ------------------------------------------------------------------------
  // Initialisation des variables de saisie
  // ------------------------------------------------------------------------
  tempInput = "";
  lineIndex = 0;
  charIndex = 0;
  ssid = "";
  password = "";

  // ========================================================================
  // SAISIE DU SSID
  // ========================================================================
  while (ssid == "") {

    // Affichage de l’écran de saisie
    showOLEDInput();

    // --------------------------------------------------
    // Bouton UP : caractère suivant dans la ligne active
    // --------------------------------------------------
    if (readBtn(BTN_UP)) {
      charIndex++;
      if (charIndex >= (int)strlen(CHAR_LINES[lineIndex])) charIndex = 0;
      waitRelease(BTN_UP);
    }

    // --------------------------------------------------
    // Bouton NEXT : changer la famille de caractères
    // --------------------------------------------------
    if (readBtn(BTN_NEXT)) {
      lineIndex++;
      if (lineIndex >= NUM_LINES) lineIndex = 0;
      charIndex = 0;
      waitRelease(BTN_NEXT);
    }

    // --------------------------------------------------
    // Bouton OK : ajout caractère ou validation
    // --------------------------------------------------
    if (readBtn(BTN_OK)) {
      unsigned long start = millis();
      while (readBtn(BTN_OK)); // attendre relâchement
      unsigned long dur = millis() - start;

      // Appui long → validation du SSID
      if (dur > 1000) {
        ssid = tempInput;
        tempInput = "";
        break;
      }
      // Appui court → ajout ou suppression de caractère
      else {
        char c = CHAR_LINES[lineIndex][charIndex];
        if (c == '<') {
          if (tempInput.length() > 0)
            tempInput.remove(tempInput.length() - 1);
        } else {
          tempInput += c;
        }
      }
    }

    delay(10); // anti-rebond simple
  }

  // ========================================================================
  // SAISIE DU MOT DE PASSE
  // (exactement le même principe que pour le SSID)
  // ========================================================================
  while (password == "") {

    showOLEDInput();

    if (readBtn(BTN_UP)) {
      charIndex++;
      if (charIndex >= (int)strlen(CHAR_LINES[lineIndex])) charIndex = 0;
      waitRelease(BTN_UP);
    }

    if (readBtn(BTN_NEXT)) {
      lineIndex++;
      if (lineIndex >= NUM_LINES) lineIndex = 0;
      charIndex = 0;
      waitRelease(BTN_NEXT);
    }

    if (readBtn(BTN_OK)) {
      unsigned long start = millis();
      while (readBtn(BTN_OK));
      unsigned long dur = millis() - start;

      if (dur > 1000) {
        password = tempInput;
        tempInput = "";
        break;
      } else {
        char c = CHAR_LINES[lineIndex][charIndex];
        if (c == '<') {
          if (tempInput.length() > 0)
            tempInput.remove(tempInput.length() - 1);
        } else {
          tempInput += c;
        }
      }
    }

    delay(10);
  }

  // ------------------------------------------------------------------------
  // Sauvegarde des identifiants en mémoire non volatile (NVS)
  // ------------------------------------------------------------------------
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);

  // ------------------------------------------------------------------------
  // Message de confirmation à l’écran
  // ------------------------------------------------------------------------
  u8g2.clearBuffer();
  drawCenteredText("Identifiants sauvegardes !", 20);
  u8g2.sendBuffer();
  delay(1000);
}

/* ---------------------------------------------------------------------------
   showWiFiStatus()

   Rôle :
   - Affiche l’état de la connexion WiFi sur l’écran OLED
   - Indique clairement si la connexion est établie ou non
   - Si le WiFi est connecté :
       • affiche "GOOD !"
       • affiche l’adresse IP locale (partiellement masquée)
   - Si le WiFi n’est pas connecté :
       • affiche "NO GOOD !"

   Objectif :
   - Donner un retour visuel simple et immédiat à l’utilisateur
   - Être lisible même sur un très petit écran OLED (72 px de large)

   Remarque :
   - Fonction purement graphique
   - Ne modifie aucun état du système
--------------------------------------------------------------------------- */
void showWiFiStatus() {

  // Efface le buffer graphique avant redessin
  u8g2.clearBuffer();

  // Police lisible pour les messages principaux
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // ------------------------------------------------------------------------
  // Cas 1 : WiFi connecté
  // ------------------------------------------------------------------------
  if (WiFi.status() == WL_CONNECTED) {

    // Message central positif
    drawCenteredText("GOOD !", 12);

    // Récupération de l’IP locale (masquée pour rester lisible)
    String ip = maskedIP(WiFi.localIP());

    // Police plus petite pour l’adresse IP
    u8g2.setFont(u8g2_font_6x10_tr);

    // Calcul de la largeur du texte pour centrage horizontal manuel
    int16_t w = u8g2.getUTF8Width(ip.c_str());
    int x = (72 - w) / 2;
    if (x < 0) x = 0;

    // Affichage de l’IP
    u8g2.drawUTF8(x, 30, ip.c_str());
  }

  // ------------------------------------------------------------------------
  // Cas 2 : WiFi non connecté
  // ------------------------------------------------------------------------
  else {
    drawCenteredText("NO GOOD !", 24);
  }

  // Envoi du buffer vers l’écran OLED
  u8g2.sendBuffer();
}

bool connectWiFi(unsigned long timeout=30000) {
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

/* ---------------------------------------------------------------------------
   showAppuiLongInvitation()

   Rôle :
   - Affiche une invitation à faire un "appui long" sur le bouton OK
   - Visualise le temps restant sous forme :
       • de texte clignotant
       • d’une barre de progression
       • d’un compte à rebours en secondes
   - Sur détection d’un appui long (> 2 secondes), entre en mode configuration WiFi

   Paramètre :
   - durationSec : durée maximale (en secondes) pendant laquelle
                   l’invitation est affichée

   Principe général :
   - La fonction tourne dans une boucle temporelle non bloquante longue
   - Elle redessine l’OLED environ toutes les 50 ms
   - L’utilisateur peut déclencher le mode configuration à tout moment
--------------------------------------------------------------------------- */
void showAppuiLongInvitation(int durationSec) {

  // Mémorise l’instant de départ
  unsigned long startTime = millis();

  // Paramètres graphiques de la barre de progression
  const int barX  = 4;    // position X de la barre
  const int barY  = 24;   // position Y de la barre
  const int fullW = 64;   // largeur totale de la barre (OLED ~72 px)

  // Boucle active pendant toute la durée demandée
  while (millis() - startTime < (unsigned long)durationSec * 1000) {

    // Effacement du buffer avant redessin
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Texte clignotant "Appui long..." (1 clignotement / seconde)
    if (((millis() / 500) % 2) == 0) {
      drawCenteredText("Appui long...", 10);
    }

    // Calcul du temps écoulé
    unsigned long elapsed = millis() - startTime;

    // Largeur courante de la barre de progression (linéaire)
    int curW = map(elapsed, 0, durationSec * 1000, 0, fullW);

    // Dessin du cadre de la barre
    u8g2.drawFrame(barX, barY, fullW, 6);

    // Dessin du remplissage de la barre
    if (curW > 0) {
      u8g2.drawBox(barX, barY, curW, 6);
    }

    // Calcul du nombre de secondes restantes
    int secondsLeft = durationSec - (elapsed / 1000);

    // Affichage du compte à rebours (ex : " 5s")
    char ssec[8];
    snprintf(ssec, sizeof(ssec), "%2ds", secondsLeft);
    drawCenteredText(ssec, barY + 14);

    // Envoi du buffer vers l’écran OLED
    u8g2.sendBuffer();

    // Petite pause pour stabiliser l’affichage
    delay(50);

    // ----------------------------------------------------------------------
    // Détection d’un appui long sur le bouton OK
    // ----------------------------------------------------------------------
    if (readBtn(BTN_OK)) {

      // Instant de début d’appui
      unsigned long ps = millis();

      // Attente du relâchement du bouton
      while (readBtn(BTN_OK));

      // Si l’appui a duré plus de 2 secondes → configuration
      if (millis() - ps > 2000) {

        // Passage en mode configuration
        inConfig = true;

        // Effacement des identifiants WiFi stockés
        prefs.clear();
        ssid = "";
        password = "";

        // Lancement du mode configuration WiFi (saisie via boutons)
        enterConfigMode();

        // Sortie immédiate de la fonction
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

}

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

  // Initialisation forcée de SHT30 à 0x45
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

  // afficher l'invitation à la presse longue
  showAppuiLongInvitation(12);

  bool wifiOK = false;
  if (ssid != "" && password != "") wifiOK = connectWiFi(30000);
  showWiFiStatus();

  if (wifiOK) {
    timeClient.begin();
    timeClient.setUpdateInterval(60000); // 60 secondes
    delay(2000);
    // afficher l'heure une fois
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    String ts = getLocalTimeString();
    int16_t w = u8g2.getUTF8Width(ts.c_str()); int x = (72 - w)/2; if (x < 0) x = 0;
    u8g2.drawUTF8(x, 32, ts.c_str()); u8g2.sendBuffer();
  }

  // Température initiale de l'écran OLED : 0,0
  afficheTemperatureOLED(0.0);
}

// ----------------- Loop -----------------
void loop() {
  static unsigned long lastRead = 0;
  static unsigned long lastOledSwitch = 0;
  static bool oledShowTime = false;
  // static unsigned long lastAnnounceCheck = 0; // supprimé : on utilise la globale déclarée plus haut
  unsigned long now = millis();

  // Lecture du capteur toutes les INTERVAL_MS
  if (now - lastRead >= INTERVAL_MS) {
    lastRead = now;
    float t = sht30.readTemperature();
    if (!isnan(t)) {
      temperatureCourante = t;
    } else {
      Serial.println("Lecture SHT30 echouee");
    }

    // Mise à jour de l'écran OLED (en fonction du temps ou de la température, selon l'alternance et le Wi-Fi)
    if (inConfig) {
      // Interface utilisateur de configuration gérée en mode de configuration (bloquant)
    } else {
      if (WiFi.status() == WL_CONNECTED) {
        // alterner tous les OLED_ALTERNATE_MS
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
        // Pas de Wi-Fi : afficher la température (ou une erreur)
        if (!isnan(temperatureCourante)) afficheTemperatureOLED(temperatureCourante);
        else {
          u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tr);
          u8g2.drawUTF8(0, 12, "Err SHT30");
          u8g2.sendBuffer();
        }
      }
    }

    // mise à jour du bargraphe à LED
    if (!isnan(temperatureCourante)) AfficheTemperatureSurLeds(temperatureCourante);
  }

  // Gestion de l'échantillonnage tendance
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

  // Exécuter l'animation de tendance si demandé (priorité), MAIS éviter la zone critique
  if (MajAffichageVariations) {
    if (isInCriticalZone()) {
      // On reporte l'animation : on n'appelle pas FonctionVariationTemperature maintenant
      // MajAffichageVariations reste true et sera retenté plus tard (hors zone critique)
      // On peut aussi ajouter ici un petit log pour debug
      // Serial.println("Tendance retardée en raison d'une zone critique");
    } else {
      // Lancer l'animation
      FonctionVariationTemperature(T1, T2);
      MajAffichageVariations = false;
      // préparer la fenêtre suivante
      T1 = T2;
      t1TempoReleveVariations = 0;
      // Après l'animation de tendance, rétablir l'affichage OLED immédiat et les LED.
      if (!isnan(temperatureCourante)) {
        AfficheTemperatureSurLeds(temperatureCourante);
        if (WiFi.status() == WL_CONNECTED) afficheTemperatureOLED(temperatureCourante); // temporary show temp then alternation continues
      }
    }
  }

  // -------------------- ANNONCE HEURE (DF1201S) --------------------
  if (now - lastAnnounceCheck >= 1000) {
    lastAnnounceCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      String hhmmss = getLocalTimeString(); // calls timeClient.update() internally
      int h = hhmmss.substring(0,2).toInt();
      int m = hhmmss.substring(3,5).toInt();
      int s = hhmmss.substring(6,8).toInt();

      //if (m == 0 && s < 3) { // pendant les 3 premières secondes de l'heure

      if (m == 0 && s == 0) { // immédiatement au début de l'heure
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

  // Bouton : cocher la case appui long sur OK pendant l’exécution normale pour forcer l’accès à la configuration
  static bool okbuttonPressed = false;
  static unsigned long okpressStart = 0;
  if (readBtn(BTN_OK)) {
    if (!okbuttonPressed) okpressStart = millis();
    okbuttonPressed = true;
    if (millis() - okpressStart > 2000 && !inConfig) {
      // entrée config mode
      inConfig = true;
      prefs.clear();
      ssid = ""; password = "";
      enterConfigMode();
      // Après être connecté, essayer de se connecter au Wi-Fi.
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
