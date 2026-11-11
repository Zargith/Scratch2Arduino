# Kit enseignant et animateur d'atelier

Ce kit aide à faire découvrir la programmation et l'électronique avec Scratch2Arduino, en classe
(collège, lycée), en club ou en atelier (fablab, médiathèque). Il suit les
[8 tutoriels](README.md#tutoriels) : chaque séance correspond à un tutoriel, que les élèves peuvent
suivre en autonomie.

*English: [../en/teachers.md](../en/teachers.md)*

## Public et prérequis

- **À partir de 10 ans** (dès 8 ans avec un adulte pour le câblage). Avoir déjà utilisé Scratch aide,
  mais n'est pas nécessaire.
- **Aucune connaissance en électronique** : chaque tutoriel explique ses branchements et le bouton
  *Schéma de câblage* les dessine à partir des blocs.
- Les élèves travaillent **en binôme** : un *pilote* à la souris, un *copilote* qui lit le tutoriel et
  vérifie le câblage ; on échange les rôles à chaque étape.

## Préparer la salle

Une semaine avant :

1. **Installe Scratch2Arduino** sur chaque ordinateur ([guide](guide.md#1-installer-scratch2arduino)).
   Sur Linux, ajoute chaque compte élève au groupe `dialout`.
2. **Télécharge les outils des cartes avec Internet**, pour que l'atelier marche ensuite hors ligne : sur
   chaque ordinateur, avec le compte qui servira pendant l'atelier, lance `s2a-cli setup` (outil installé
   à côté de l'application ; sous Windows :
   `& "$env:ProgramFiles\Scratch2Arduino\bin\s2a-cli.exe" setup` dans PowerShell). Ou téléverse une fois
   l'exemple *Clignotant* sur chaque type de carte utilisé. Les outils sont rangés dans le dossier de
   l'utilisateur : refais-le pour chaque compte.
3. **Teste chaque kit** : branche la carte, `s2a-cli ports` (ou le menu *Port*) doit la montrer ; ouvre
   *Nouveau → Clignotant*, clique sur *Téléverser* ; la LED **L** clignote.
4. **Prépare des boîtes** numérotées (une par binôme) avec le matériel de la séance, et quelques câbles
   USB, LED et résistances de rechange.
5. Imprime les schémas de câblage des exemples (fenêtre *Schéma de câblage → Enregistrer l'image*, ou
   les images de [`examples/`](../../examples/README.md)).

## Règles de sécurité à annoncer

- On **débranche la carte** avant de changer un fil ; on fait vérifier son montage avant de rebrancher.
- Une LED a toujours **sa résistance**.
- Jamais de fil entre **5V et GND** directement (court-circuit : la carte chauffe, le port USB se coupe).
- Sur une ESP32 : les capteurs vont au **3V3**, jamais de 5 V sur une broche.
- Les piles du robot : on ne les court-circuite pas, on les enlève en fin de séance.

## Matériel

Par binôme, pour les tutoriels 1 à 7 (kit « débutant Arduino » courant) :

| Matériel | Quantité | Tutoriels |
|---|---|---|
| Arduino Uno (ou Nano) + câble USB de données | 1 | tous |
| Platine d'essai 830 points | 1 | 2 à 7 |
| Fils mâle-mâle / mâle-femelle | 20 / 10 | 2 à 7 |
| LED rouge, orange, verte | 2 de chaque | 2, 3 |
| Résistances 220 Ω | 5 | 2, 3, 5 |
| Bouton-poussoir | 2 | 3, 7 |
| Potentiomètre 10 kΩ | 1 | 4 (et 6 en variante 16 broches) |
| Servomoteur SG90 | 1 | 4 |
| Capteur à ultrasons HC-SR04 | 1 | 5 |
| LED RGB à cathode commune | 1 | 5 |
| Buzzer **passif** | 1 | 5, 7 |
| Capteur DHT11 (ou DHT22) | 1 | 6 |
| Écran LCD 16×2 avec module I2C | 1 | 6 |
| Écran OLED SSD1306 0,96" I2C | 1 | 7 |
| Matrice de LED 8×8 MAX7219 (module) | 1 | 7 |

Pour le tutoriel 8 (robot), par groupe : ESP32 DevKit + câble micro-USB, pont en H L298N, 2 moteurs à
réducteur jaunes avec roues, un châssis avec roue folle (ou du carton), boîtier 4 piles AA + piles,
batterie USB, un téléphone **Android** avec l'appli « Serial Bluetooth Terminal ».

## Formats

| Format | Durée | Contenu |
|---|---|---|
| Découverte | 2 h | Tutoriels 1 et 2 (jusqu'aux fonctions si le temps le permet), défis du tutoriel 1 |
| Journée | 6 h | Tutoriels 1 à 5 ; l'après-midi, chaque binôme choisit le 6 ou le 7 et le présente |
| Cycle | 8 séances de 1 h 30 à 2 h | Une séance par tutoriel, défis compris ; dernière séance : projet libre ou robot |

## Déroulé type d'une séance (1 h 30)

| Temps | Étape | Rôle de l'animateur |
|---|---|---|
| 10 min | Rappel et objectif | Montrer le résultat attendu (montage qui fonctionne), poser une question : « à votre avis, comment la carte sait-elle que… ? » |
| 10 min | Câblage | Les binômes câblent **carte débranchée** à partir du schéma ; l'animateur valide chaque montage |
| 40 min | Programme | Les élèves suivent le tutoriel. Encourager le **mode Live** pour essayer, puis *Téléverser* |
| 20 min | Défis | Les défis du tutoriel, au choix ; les plus rapides aident les autres |
| 10 min | Bilan | Questions de bilan (ci-dessous), enregistrement du projet (`.s2a`) |

## Les séances

### 1. Clignotant
- **Notions** : programme, instruction, séquence, boucle infinie, sortie, téléverser.
- **Idée reçue** : « sans le second *attendre*, la LED clignote » — faire constater qu'elle reste
  allumée.
- **Bilan** : que fait la carte quand on la rebranche ? Différence entre *Live* et *Téléverser* ?

### 2. Feu tricolore
- **Notions** : LED (polarité), résistance, broche HAUT/BAS, séquence, fonction.
- **Idée reçue** : « la LED marche dans les deux sens » — faire retourner une LED.
- **Bilan** : à quoi sert la résistance ? Pourquoi les fonctions rendent-elles le programme plus lisible ?

### 3. Bouton et LED
- **Notions** : entrée, condition (vrai / faux), si … alors … sinon, boucle de surveillance, variable
  (interrupteur).
- **Idée reçue** : « le programme réagit au bouton tout seul » — retirer *répéter indéfiniment* pour
  montrer qu'il ne lit le bouton qu'une fois.
- **Bilan** : quelle est la différence entre « appuyé » et « a été appuyé » ? Pourquoi attendre qu'on
  relâche le bouton ?

### 4. Potentiomètre et servomoteur
- **Notions** : grandeur analogique, plage de valeurs, variable, proportionnalité (convertir).
- **Lien maths** : la règle de trois du bloc *convertir*.
- **Bilan** : quelles valeurs donne le potentiomètre ? Que devient 25 après conversion vers 0 – 180 ?

### 5. Alarme de distance
- **Notions** : capteur, mesure, seuil, comparaison, valeur qui règle une durée.
- **Lien physique** : vitesse du son, écho (le capteur mesure un aller-retour).
- **Bilan** : que se passe-t-il si on change le seuil ? Comment ajouter une troisième zone ?

### 6. Thermomètre
- **Notions** : capteur numérique, bus I2C, afficheur, texte et nombres.
- **Lien SVT / physique** : température, humidité, relevés (noter les valeurs au fil de la journée).
- **Bilan** : pourquoi attendre 2 secondes entre deux mesures ? Pourquoi les accents disparaissent-ils ?

### 7. Jeu de réaction
- **Notions** : hasard, attente d'un événement, score (variable qui s'accumule), projet complet.
- **Idée reçue** : « le hasard est vraiment imprévisible » — discuter des nombres pseudo-aléatoires.
- **Bilan** : comment empêcher la triche ? Comment mesurer le temps de réaction ?

### 8. Robot télécommandé
- **Notions** : moteur, pont en H, alimentation séparée et masse commune, communication sans fil,
  organisation en fonctions.
- **Point d'attention** : le GND des piles relié à celui de l'ESP32 ; les cavaliers ENA/ENB enlevés.
- **Bilan** : pourquoi la carte ne peut-elle pas alimenter un moteur directement ? Que se passe-t-il si
  le téléphone envoie un mot inconnu ?

## Évaluer

- **Compétences** : lire un schéma et câbler ; décrire ce que fait un programme avant de le lancer ;
  corriger une erreur à partir du message affiché ; modifier un programme pour un défi.
- **Traces** : chaque binôme enregistre son projet (`.s2a`) et le rend (clé USB, ENT) ; le fichier se
  rouvre à l'identique sur un autre ordinateur.
- **Grille simple** : *réalisé avec aide* / *réalisé seul* / *amélioré (défi réussi)* / *expliqué à un
  autre binôme*.

## Astuces d'animation

- Le bouton **Schéma de câblage** dessine le montage correspondant aux blocs de l'élève : en cas de
  doute, comparer le schéma et la platine.
- Les blocs non accrochés sont **grisés** et ignorés : pas besoin de tout supprimer pour essayer.
- Les erreurs n'apparaissent qu'au clic sur *Vérifier*, *Téléverser* ou *Live*, avec une explication et
  le bloc mis en évidence : laisser l'élève lire le message avant d'intervenir.
- **Mes blocs** permet de distribuer un morceau de programme : l'enseignant l'exporte
  (`.s2ablocks`), les élèves l'importent.
- Le **moniteur série** et le panneau **Live** rendent visibles les valeurs des capteurs : idéal pour
  les questions « combien ? ».
