# ESP32-THERMOMETRE-A-LEDS-WS2812B-AVEC-ANNONCE-VOCALE-HEURE-DFTA246

Vidéo YouTube : https://youtu.be/PGV0VyRELZA

🛰️ Navette lumineuse et parlante — ESP32
📌 Présentation

Ce projet est une navette lumineuse et parlante basée sur ESP32, conçue pour être fiable, lisible et évolutive.

Dans sa version 1, la navette :

permet la mesure de la température ambiante,

affiche alternativement l’heure et la température sur un écran OLED,

annonce vocalement l’heure à chaque heure pleine,

elle utilise une interface simple à 3 boutons pour la configuration WiFi,

elle fonctionne de manière autonome après configuration.

Ce projet cherche à être robuste, compréhensible et reproductible.

❤️ Un projet personnel

Cette navette n’est pas un simple exercice technique.
Elle est née d’une promesse faite à mon père, et d’une volonté de créer un objet électronique vivant, utile et humain.

C’est aussi pour cette raison que le code est volontairement :

trés structuré,

commenté,

et écrit step by step.

🧩 Architecture matérielle

ESP32-C3

Capteur de température SHT30

Écran OLED (I²C) intégré à l'ESP32 C3

LEDs adressables WS2812B

DFPlayer Pro pour l’annonce vocale

Alimentation 5V, l'ESP32 C3 distribue son 3.3V au SHT30

Le PCB a été conçu pour permettre des variantes :

avec ou sans audio,

avec ou sans LEDs,

différents boîtiers possibles.

🖥️ Fonctionnalités – Version 1

✔ Affichage température / heure (alternance)
✔ Synchronisation horaire NTP
✔ Annonce vocale de l’heure
✔ Interface WiFi autonome via OLED + boutons
✔ Indication de tendance thermique par animation LED
✔ Code commenté et structuré

🚧 Philosophie du projet

Ce projet suit une approche volontairement progressive :

Une fonctionnalité → un test → une validation

Les fonctionnalités plus avancées (événements saisonniers, animations festives, effets spéciaux) sont volontairement hors du périmètre de la V1 et feront l’objet de versions ultérieures documentées.

🔜 Évolutions prévues

🎄 Événements saisonniers (Noël, Nouvel An…)

🎆 Animations LED dédiées (neige, confettis, feu d’artifice)

🔊 Scénarios audio synchronisés

🧰 Variantes matérielles

📹 Vidéos explicatives (code, soudure WS2812B, PCB)

📁 Contenu du dépôt

*.ino : code principal ESP32

Schémas électroniques

Fichiers PCB (Gerber)

Datasheets

Photos

Ressources audio (structure uniquement, fichiers également fournis)


🛠️ Prérequis

Arduino IDE

Bibliothèques principales :

WiFi / NTPClient

Adafruit_SHT31

U8g2

Adafruit NeoPixel

DFPlayer Pro (DF1201S)

📜 Licence

Ce projet est publié pour un usage personnel et éducatif.
Merci de respecter l’esprit du projet et de citer la source en cas de réutilisation.

✉️ Remarques

Ce dépôt évoluera au rythme du projet réel.
Les versions sont volontairement conservées pour montrer la progression et les choix techniques.


