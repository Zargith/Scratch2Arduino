# Guide d'utilisation de Scratch2Arduino

Scratch2Arduino permet de programmer une carte Arduino (ou ESP32) avec des blocs, comme dans Scratch :
tu assembles les blocs, et la carte allume des LED, fait tourner des moteurs, lit des capteurs...
Ce guide présente tout le logiciel. Pour apprendre pas à pas, suis plutôt les
[tutoriels](README.md#tutoriels), en commençant par le [Clignotant](tutorials/01-blink.md). Ils sont aussi
dans le logiciel : bouton **Tutoriels et aide** (le chapeau de diplômé), à côté du moniteur série.

*To read it in English, change the language in the settings.*

## Sommaire

1. [Installer Scratch2Arduino](#1-installer-scratch2arduino)
2. [Brancher la carte](#2-brancher-la-carte)
3. [La fenêtre](#3-la-fenêtre)
4. [Construire un programme avec des blocs](#4-construire-un-programme-avec-des-blocs)
5. [Les familles de blocs](#5-les-familles-de-blocs)
6. [Vérifier et téléverser](#6-vérifier-et-téléverser)
7. [Le mode Live](#7-le-mode-live)
8. [Le moniteur série](#8-le-moniteur-série)
9. [Le schéma de câblage](#9-le-schéma-de-câblage)
10. [Variables, fonctions et "Mes blocs"](#10-variables-fonctions-et-mes-blocs)
11. [Écrire du code : "Mon code"](#11-écrire-du-code--mon-code)
12. [Projets : enregistrer, ouvrir, partager](#12-projets--enregistrer-ouvrir-partager)
13. [Sans fil : Wi-Fi et Bluetooth (ESP32, ESP8266)](#13-sans-fil--wi-fi-et-bluetooth-esp32-esp8266)
14. [Raccourcis clavier](#14-raccourcis-clavier)
15. [En cas de problème](#15-en-cas-de-problème)

## 1. Installer Scratch2Arduino

- **Windows** : télécharge `Scratch2Arduino_installer.exe` (page *Releases* du projet) et lance-le.
  Il installe ce qui manque, puis Scratch2Arduino, avec un raccourci sur le bureau et dans le menu Démarrer.
  Si Windows affiche "Windows a protégé votre ordinateur", clique sur *Informations complémentaires*
  puis *Exécuter quand même* (le programme n'est pas encore signé).
- **Linux et macOS** : télécharge `Scratch2Arduino_installer.sh`, puis dans un terminal :
  `sh Scratch2Arduino_installer.sh` (ou `--user` pour l'installer seulement pour toi).
  Sur Linux, pour pouvoir envoyer les programmes sur la carte, tape une fois
  `sudo usermod -aG dialout $USER` puis ferme ta session et rouvre-la.

**Mises à jour** : au démarrage, Scratch2Arduino regarde en arrière-plan si une nouvelle version existe et
la propose (*Télécharger*, *Nouveautés* ou *Plus tard* : rien n'est obligatoire). On peut aussi vérifier
dans les *Paramètres* (engrenage), ou désactiver cette recherche.

Au premier envoi d'un programme, Scratch2Arduino télécharge les outils de la carte (il faut Internet
cette fois-là, puis plus jamais pour cette carte). Il garde sa propre copie de ces outils : il ne
modifie pas un Arduino IDE installé sur le même ordinateur.

## 2. Brancher la carte

1. Branche la carte avec un câble USB **de données** (certains câbles ne servent qu'à recharger : si la
   carte n'apparaît jamais, essaie un autre câble).
2. Dans la barre du haut, choisis la **Carte** (Arduino Uno, Nano, Mega 2560, ESP32 DevKit ou
   ESP8266 NodeMCU).
3. Choisis le **Port** : la liste montre les cartes branchées. Si elle est vide, clique sur
   **Chercher les cartes branchées** (la flèche qui tourne, à côté).

> Débranche toujours la carte avant de modifier les fils, et vérifie le montage avant de la rebrancher.

## 3. La fenêtre

- **Barre du haut**, de gauche à droite : le nom du projet ; *Nouveau*, *Ouvrir*, *Enregistrer*,
  *Projets récents* ; la carte et le port ; *Tutoriels et aide* ; *Moniteur série* ; *Wi-Fi* (seulement pour
  une ESP32 ou une ESP8266) et *Bluetooth* (ESP32, ou carte Arduino avec un module HC-05 / HC-06) ; *Schéma de câblage* ; *Voir le code* ; **Live**, **Vérifier**,
  **Téléverser** ; les **Paramètres** (l'engrenage : langue, apparence claire ou sombre, recherche d'une nouvelle version). Sur un petit écran, Live, Vérifier et Téléverser ne montrent que
  leur icône : passe la souris dessus pour lire leur nom.
- **Palette** (à gauche) : les familles de blocs, avec une couleur chacune. Clique sur une famille pour
  voir ses blocs.
- **Zone de travail** (au centre) : là où tu assembles les blocs. La molette fait défiler ; les boutons
  **+**, **−** et **=** (en bas à droite) zooment ; la **poubelle** jette les blocs qu'on y dépose.
- **Panneaux** (à droite, quand ils sont ouverts) : les valeurs en direct du mode Live, le moniteur série.
- **Barre d'état** (en bas) : ce qui se passe ("Vérification du programme...") et les problèmes à
  corriger.

## 4. Construire un programme avec des blocs

- **Ajouter un bloc** : fais-le glisser depuis la palette vers la zone de travail.
- **Accrocher** : approche un bloc sous un autre, ou dans la "bouche" d'un bloc comme
  *répéter indéfiniment* : il s'emboîte quand une ombre grise apparaît.
- **Remplir une case** : clique dans une case blanche pour taper un nombre ou un texte ; glisse un bloc
  arrondi (une valeur) ou hexagonal (une condition, vrai ou faux) dans l'emplacement qui a sa forme.
- **Choisir dans un menu** : clique sur la petite flèche (un numéro de broche, *HAUT* / *BAS*, une
  couleur, une note...).
- **Déplacer** : glisser un bloc emporte aussi tous les blocs accrochés en dessous.
- **Supprimer** : glisse le bloc vers la palette ou la poubelle, ou sélectionne-le et appuie sur
  *Suppr*.
- **Clic droit sur un bloc** : dupliquer, ajouter un commentaire, supprimer, *Enregistrer dans Mes blocs*.
- **Annuler / rétablir** : Ctrl+Z / Ctrl+Y (voir les [raccourcis](#14-raccourcis-clavier)).

Un programme commence **toujours** par le bloc **quand le programme démarre** (famille *Événements*).
Les blocs qui ne sont accrochés à aucun bloc de départ sont **grisés** : ils ne sont pas envoyés à la
carte, comme des brouillons. Tu peux en laisser traîner pendant que tu cherches.

Pendant que tu construis, l'éditeur ne t'interrompt jamais : les erreurs ne sont cherchées que quand tu
cliques sur **Vérifier**, **Téléverser** ou **Live**.

## 5. Les familles de blocs

| Famille | À quoi elle sert | Exemples de blocs |
|---|---|---|
| Événements | Le départ du programme | quand le programme démarre |
| Contrôle | Attendre, répéter, choisir | attendre 1 secondes, répéter 10 fois, répéter indéfiniment, si ... alors ... sinon, attendre jusqu'à ce que |
| Broches | Les broches de la carte | allumer la LED intégrée, mettre la broche 13 à l'état HAUT, la broche 2 est à l'état HAUT, valeur analogique de la broche A0, mettre la sortie PWM de la broche 9 à 128 |
| Capteurs | Lire le monde | le bouton de la broche 2 est appuyé, position du potentiomètre (0 à 100), lumière mesurée, distance en cm (HC-SR04), température et humidité (DHT11, DHT22) |
| Lumières | LED de couleur | LED RGB, ruban NeoPixel, matrice de LED 8x8 (MAX7219) |
| Son | Le buzzer | jouer 440 Hz pendant 1 secondes, faire sonner / arrêter le buzzer, note do 4 |
| Moteurs | Faire bouger | servomoteur (0 à 180°), moteur à courant continu par un pont en H (L298N, L293D) |
| Écrans | Afficher du texte | écran LCD 16x2 (module I2C ou 16 broches), écran OLED SSD1306 |
| Opérateurs | Calculer, comparer | + − x /, nombre aléatoire, convertir, <, >, =, et, ou, non |
| Variables | Retenir des valeurs | *Créer une variable*, mettre ... à ..., ajouter ... à ... |
| Fonctions | Réutiliser un groupe de blocs | *Créer une fonction*, définir ..., appeler ... |
| Moniteur série | Écrire à l'ordinateur | écrire ... sur le moniteur série |
| Téléphone (ESP32, ou Arduino avec un module HC-05 / HC-06) | Parler à un téléphone par Bluetooth | module Bluetooth HC-05 / HC-06, le téléphone a envoyé ..., nombre reçu du téléphone, envoyer ... au téléphone |
| Mes blocs | Tes groupes de blocs enregistrés | voir [Mes blocs](#mes-blocs) |

Passe la souris sur un bloc de la palette : une bulle explique ce qu'il fait et **comment brancher le
composant**. La liste complète est dans le [catalogue des blocs](../block-catalog.md) (en anglais).

## 6. Vérifier et téléverser

- **Vérifier** cherche les problèmes du programme puis le traduit en code Arduino (C++) et le compile,
  sans rien envoyer. C'est utile même sans carte branchée.
- **Téléverser** fait la même chose puis envoie le programme dans la carte. Il y reste, même débranchée :
  il redémarre chaque fois que la carte est alimentée (par USB, une pile ou un chargeur).
- **Voir le code** montre le code Arduino fabriqué à partir de tes blocs : une bonne façon de découvrir
  le "vrai" langage de l'Arduino.

Quand il y a un problème, la barre d'état explique **ce qui ne va pas** et **comment le corriger**, et
le bloc concerné est mis en évidence dans la zone de travail. Exemples :

- "Ton programme ne commence nulle part." -> ajoute *quand le programme démarre* et accroche tes blocs
  dessous ;
- "Il manque une condition dans le bloc "si ... alors"." -> glisse un bloc hexagonal dans la case vide ;
- "La broche 7 de la carte Arduino Uno ne sait pas faire "sortie PWM" (bloc "...")." -> choisis une des broches
  proposées (celles marquées d'un `~` sur la carte).

## 7. Le mode Live

Clique sur **Live** : les blocs s'exécutent **sur l'ordinateur**, qui commande la carte en direct par le
câble. Tu vois tout de suite l'effet d'un changement, sans téléverser :

- les scripts qui tournent sont **éclairés** ;
- le panneau de droite montre les **variables** et les **capteurs** lus par les blocs, avec un graphique
  des 30 dernières secondes ;
- modifie un bloc (un nombre, une broche, un bloc ajouté...) : la carte suit immédiatement.

**Sorties du programme** : ce que ton programme écrit (bloc *écrire ... sur le moniteur série*, `print()`
en Python ou JavaScript) s'affiche dans le panneau du moniteur série, qui s'ouvre tout seul à la première
ligne. Pour déboguer, `printError()` (et `console.error()` en JavaScript) écrit en rouge, sur la sortie
d'erreur ; l'erreur qui arrête un programme y reste aussi, avec sa ligne, après l'arrêt.

**Pas à pas** : pour comprendre ce que fait ton programme, clique sur **Pause** (à côté d'*Arrêter*,
touche F9). Les blocs s'arrêtent et le bloc qui va s'exécuter est éclairé. Chaque clic sur **Bloc
suivant** (F10) exécute ce bloc seulement : regarde la carte et les variables changer, un bloc à la fois.
Les blocs *attendre* ne font plus attendre en pas à pas. Avec plusieurs scripts, ils avancent chacun leur
tour. **Reprendre** (F9) relance le programme à vitesse normale. Le pas à pas marche pour les blocs (même
avec "Mon code"), pas pour un programme entier écrit en Python ou en JavaScript.

La première fois, Scratch2Arduino installe sur la carte un petit programme, *S2A live*, qui écoute les
ordres de l'ordinateur (quelques secondes). Clique sur **Arrêter** pour quitter le mode Live : les
sorties de la carte sont éteintes. Pour que la carte fonctionne seule, sans ordinateur, utilise
**Téléverser**.

## 8. Le moniteur série

Le bouton **Moniteur série** ouvre un panneau qui affiche ce que la carte écrit avec le bloc
*écrire ... sur le moniteur série*. Téléverse ton programme, puis clique sur **Connecter** (vitesse 9600).
Une ligne tapée en bas est envoyée à la carte. Pendant un téléversement le moniteur se met en pause puis
se reconnecte. En mode Live, le panneau montre ce que les blocs écrivent.

## 9. Le schéma de câblage

Le bouton **Schéma de câblage** montre comment brancher les composants **utilisés par tes blocs** :

- **Schéma de câblage** : la carte, une platine d'essai et les composants, avec un fil de couleur par
  branchement (molette ou double-clic pour zoomer, glisser pour se déplacer, *Enregistrer l'image* pour
  l'imprimer) ;
- **Composants à brancher** : le même câblage en tableau - chaque patte de chaque composant, la broche
  de la carte où elle va et ce qu'il faut mettre entre les deux (par exemple la résistance de 220 Ω d'une
  LED) ; *Voir les blocs* montre les blocs concernés ;
- **Montage de l'exemple** (pour un exemple) : le montage dessiné à la main.

Le schéma prévient aussi quand une broche sert à deux composants, quand un écran I2C n'a plus ses broches
SDA/SCL, ou quand les composants demandent plus de courant qu'un port USB n'en donne.

Rappels pour la platine d'essai : les 5 trous d'une même colonne (a à e, ou f à j) sont reliés ; les
rangées **+** (rouge) et **−** (bleu) des bords sont reliées sur toute leur longueur. Fil rouge = 5V,
fil noir = GND.

## 10. Variables, fonctions et "Mes blocs"

### Variables

Une variable est une boîte qui retient une valeur (un score, une distance...). Dans la famille
*Variables*, clique sur **Créer une variable** et donne-lui un nom. Utilise ensuite *mettre ... à ...*,
*ajouter ... à ...* et le bloc arrondi qui porte son nom. En mode Live, sa valeur s'affiche à droite.

### Fonctions

Une fonction donne un nom à un groupe de blocs pour le réutiliser (comme "Mes blocs" de Scratch).
Dans la famille *Fonctions*, clique sur **Créer une fonction**, accroche des blocs sous **définir ...**,
puis utilise **appeler ...** autant de fois que tu veux. Une fonction ne s'exécute pas toute seule au
démarrage, et elle ne peut pas s'appeler elle-même. Clic droit sur *définir* pour la renommer ou la
supprimer. Exemple : le [Feu tricolore](tutorials/02-traffic-light.md).

### Mes blocs

Pour garder un groupe de blocs **pour tous tes projets** : clic droit sur le premier bloc ->
**Enregistrer dans Mes blocs** (les blocs accrochés dessous sont gardés aussi). Le groupe apparaît dans
la famille *Mes blocs* : clique dessus pour l'ajouter à ton projet. **Gérer mes blocs** permet de les
renommer, de les supprimer, de les **exporter** (fichier `.s2ablocks`) et de les **importer** sur un autre
ordinateur.

## 11. Écrire du code : "Mon code"

Quand les blocs ne suffisent plus, écris tes propres fonctions en **C++**, **Python** ou **JavaScript** à
côté des blocs : elles deviennent de nouveaux blocs.

1. En haut, passe la **Vue** de *Blocs* à **Code**. L'onglet **Code des blocs** montre le code Arduino
   fabriqué à partir de tes blocs (en lecture seule).
2. Onglet **Mon code** : choisis un langage. Un exemple s'affiche (une variable `vitesse`, des fonctions
   `clignoter` et `moyenne`) : modifie-le ou remplace-le. L'éditeur colore le code et propose des
   complétions (mots du langage, fonctions Arduino, noms déjà écrits).
3. Reviens à la vue **Blocs** : la famille **Mon code** contient un bloc par fonction (avec des cases pour
   ses nombres), un bloc pour le résultat d'une fonction, et des blocs pour lire ou changer chaque variable
   (nombres, et textes `String` en C++). Une constante, ou une variable que ton code change lui-même dans
   une de ses fonctions (son état, comme `lampe` d'un interrupteur), a seulement un bloc pour la lire : un
   bloc qui la changerait dérangerait ton code.

- En **C++**, ton code est compilé avec les blocs : *Vérifier* et *Téléverser* marchent comme d'habitude.
- En **Python** ou **JavaScript**, ton code tourne sur l'ordinateur, avec le bouton **Live** (avec les
  fonctions `digitalWrite`, `analogRead`, `delay`... de l'Arduino).
- Les erreurs de ton code s'affichent sur leur ligne, seulement quand tu cliques sur *Vérifier*,
  *Téléverser* ou *Live*. *Supprimer mon code* (dans l'onglet) l'enlève du projet.
- **Changer de langage** : choisis un autre langage pour "Mon code" (ou pour le programme en mode
  expert) et ton code est **converti** : C++, Python et JavaScript dans tous les sens. Les variables, les
  fonctions, les si / sinon, les boucles et les fonctions Arduino sont traduits ; ce que la conversion ne
  sait pas traduire reste en commentaires, et le message dit quelles lignes reprendre à la main.
- **Mode expert** : tout le programme peut aussi être écrit en texte (*Programme entier en C++, Python
  ou JavaScript*) ; *Aide du langage* rappelle les fonctions disponibles.

### Bibliothèques Arduino

Une **bibliothèque** ajoute des fonctions toutes prêtes à ton code C++ (lire un capteur rare, décoder
du JSON...). Les blocs installent les leurs tout seuls ; pour ton propre code, utilise le bouton
**Bibliothèques Arduino** (icône de livres, en haut à droite) :

1. Onglet **Chercher** : tape un nom ou un mot-clé (par exemple `json`), puis *Chercher*. La première
   fois, la liste des bibliothèques est téléchargée (il faut Internet). Les descriptions sont en anglais.
2. *Ajouter au projet* installe la bibliothèque (sa dernière version, ou celle choisie dans la liste) et
   l'ajoute au projet.
3. Onglet **Ce projet** : la ligne `#include <...>` à écrire en haut de "Mon code" est indiquée.
   *Retirer du projet* l'enlève de la liste.
4. Onglet **Installées** : tout ce qui est installé sur l'ordinateur, avec *Mettre à jour* et
   *Désinstaller*.

Les bibliothèques du projet sont enregistrées dans le fichier `.s2a` : sur un autre ordinateur, *Vérifier*
les installe toutes seules. Si ton code utilise une bibliothèque absente, le message de *Vérifier* donne
son nom et propose des bibliothèques qui pourraient la contenir.

## 12. Projets : enregistrer, ouvrir, partager

- **Nouveau** ouvre la galerie : un **projet vide** ou un **exemple** (avec son matériel et son schéma).
  Certains exemples existent en plusieurs versions, au choix sur la carte : un autre montage (écran I2C
  ou 16 broches), ou un autre niveau - **Novice** (blocs seuls) ou **Avancé** (avec "Mon code"). Un
  exemple ouvert est une copie : enregistre-le pour garder tes changements.
- **Enregistrer** (Ctrl+S) écrit un fichier **`.s2a`**, rangé par défaut dans
  `Documents/Scratch2Arduino`. Ce fichier contient tout le projet : c'est lui qu'on envoie à quelqu'un,
  qu'on met sur une clé USB ou qu'on rend à son professeur.
- **Ouvrir** relit un `.s2a`. Sous Windows, un double-clic sur un `.s2a` l'ouvre aussi, et on peut
  déposer un projet sur l'icône de Scratch2Arduino.
- **Projets récents** (l'horloge) rouvre les derniers projets.
- **Sauvegarde automatique** : toutes les 15 secondes, une copie de secours est gardée. Si le logiciel
  ou l'ordinateur s'arrête, Scratch2Arduino propose au démarrage suivant de **récupérer** le projet.

## 13. Sans fil : Wi-Fi et Bluetooth (ESP32, ESP8266)

Une carte ESP32 ou ESP8266 peut être commandée en mode Live **sans câble** :

- **Wi-Fi** (ESP32, ESP8266) : lance une fois le mode Live par le câble USB. La carte crée ensuite son
  propre réseau **S2A-xxxx** (mot de passe `scratch2arduino`) : connecte l'ordinateur à ce réseau et
  choisis le port **Wi-Fi - réseau de la carte**. Le bouton **Wi-Fi** permet aussi de lui faire rejoindre
  le réseau de la maison ou de l'école (l'ordinateur garde alors Internet).
- **Bluetooth** (ESP32) : lance une fois le mode Live par le câble USB, puis bouton **Bluetooth** ->
  **Chercher les cartes** -> choisis ta carte "S2A-...". Sur Mac, connecte d'abord "S2A-..." dans
  *Réglages Système > Bluetooth*.
- **Module Bluetooth HC-05 / HC-06** (Uno, Nano, Mega) : une carte Arduino n'a pas de Bluetooth, mais
  un module HC-05 ou HC-06 lui en donne un pour le mode Live. Bouton **Bluetooth** :
  1. Branche le module : VCC sur 5V, GND sur GND, **TXD** du module sur la broche qui reçoit (10 par
     défaut), **RXD** du module sur la broche qui envoie (11 par défaut) à travers un pont diviseur
     (1 kΩ entre la broche et RXD, 2 kΩ entre RXD et GND), car RXD n'accepte que 3,3 V.
  2. Carte branchée en USB, choisis les broches et clique sur **Enregistrer sur la carte** : le
     programme Live s'installe si besoin et la carte retient les broches.
  3. Dans les réglages Bluetooth de l'ordinateur, appaire "HC-05" (ou "HC-06") avec le code **1234**
     (ou 0000), puis **Chercher les cartes** -> choisis ton module. Sur Mac, choisis ensuite le port
     "Bluetooth - HC-05" dans la liste des ports.

  Les blocs ne peuvent pas utiliser les deux broches du module : le mode Live le signale et montre les
  blocs à changer. Les broches 0 et 1 restent pour le câble USB. Sur une Mega, la broche qui reçoit doit
  être 10 à 13, 50 à 53 ou A8 à A15, ou alors utilise un de ses ports série (19 et 18, 17 et 16, 15 et 14).
  Le module est une simple liaison série : si l'ordinateur s'éloigne, la carte ne le sait pas et garde
  ses sorties ; le bouton **Arrêter** les éteint.
- **Téléphone avec un module** (Uno, Nano, Mega) : ajoute le bloc **module Bluetooth HC-05 / HC-06**
  sous "quand le programme démarre", avec les broches où le module est branché (même câblage que
  ci-dessus). Les blocs *Téléphone* passent alors par le module, en téléversement comme en mode Live
  par le câble USB. Sur le téléphone, l'appli se connecte à "HC-05" (code 1234 ou 0000). Sans ce bloc,
  la vérification le signale.
- **Téléphone** (ESP32) : les blocs *Téléphone* échangent des messages avec une appli de terminal
  Bluetooth sur le téléphone (par exemple "Serial Bluetooth Terminal" sur Android). Voir le
  [Robot télécommandé](tutorials/08-remote-robot.md).

Les programmes s'envoient toujours avec le câble USB.

## 14. Raccourcis clavier

| Raccourci (⌘ au lieu de Ctrl sur Mac) | Action |
|---|---|
| Ctrl+S | Enregistrer le projet |
| Ctrl+C / Ctrl+X | Copier / couper le bloc sélectionné, avec les blocs en dessous |
| Ctrl+V | Coller |
| Ctrl+Z | Annuler |
| Ctrl+Y ou Ctrl+Maj+Z | Rétablir |
| Suppr ou Retour arrière | Supprimer le bloc sélectionné |
| F9 / F10 | Mode Live : pause ou reprendre / exécuter le bloc suivant |

Quand tu tapes dans une case (un nombre, un texte, le nom du projet), ces touches agissent sur le texte.

## 15. En cas de problème

| Ce qui se passe | Que faire |
|---|---|
| La liste des ports est vide | Clique sur *Chercher les cartes branchées*. Essaie un autre câble (certains ne transportent pas de données) ou un autre port USB. Sur Linux, vérifie le groupe `dialout` ([installation](#1-installer-scratch2arduino)). Les cartes "compatibles" à puce CH340 demandent parfois un pilote sous Windows et macOS. |
| "Impossible d'utiliser le port de la carte" | Un autre programme utilise la carte : ferme l'Arduino IDE ou un autre moniteur série, puis réessaie. |
| "La carte ne répond pas" | Débranche la carte, rebranche-la, choisis de nouveau son port, puis réessaie. Vérifie que la bonne carte est choisie (Uno, Nano...). |
| Le premier téléversement est long | Normal : les outils de la carte se téléchargent une seule fois (il faut Internet). |
| Le programme est envoyé mais rien ne se passe | Ouvre le *Schéma de câblage* et compare avec ton montage : une LED montée à l'envers (patte longue = +), un fil dans la mauvaise colonne de la platine, un GND oublié... |
| Une LED ne s'allume pas | Elle est peut-être à l'envers, ou sans résistance (elle a pu griller). Essaie-la avec le bloc *mettre la broche ... à l'état HAUT* tout seul. |
| Le buzzer ne joue qu'un seul son | Utilise un buzzer **passif** pour jouer des notes ; un buzzer actif ne fait qu'un seul son. |
| L'écran LCD reste vide | Tourne le petit potentiomètre de contraste (à l'arrière du module I2C, ou celui branché sur V0 pour un écran à 16 broches). |
| "Oups, l'éditeur s'est arrêté" | Clique sur *Recommencer* : tes projets enregistrés ne sont pas touchés. Si cela recommence, envoie les détails affichés aux développeurs (voir [contribuer](../../CONTRIBUTING.md)). |
