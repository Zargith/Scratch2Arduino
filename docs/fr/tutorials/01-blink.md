# Tutoriel 1 — Clignotant

**Objectif** : faire clignoter la petite LED soudée sur la carte.
**Durée** : 20 minutes. **Matériel** : une carte Arduino Uno (ou Nano, Mega) et son câble USB. Rien à brancher !
**Tu vas apprendre** : le bloc de départ, une sortie, attendre, répéter indéfiniment, téléverser, le mode Live.

*English: [../../en/tutorials/01-blink.md](../../en/tutorials/01-blink.md)*

## 1. Prépare la carte

1. Branche la carte à l'ordinateur avec le câble USB. Une petite LED verte « ON » s'allume sur la carte.
2. Lance **Scratch2Arduino**.
3. Dans la barre du haut, choisis la **Carte** : *Arduino Uno*.
4. Choisis le **Port** de ta carte. Si la liste est vide, clique sur le bouton avec la flèche qui tourne
   (*Chercher les cartes branchées*).

Sur la carte, cherche la LED marquée **L** : c'est elle qui va clignoter. Elle est reliée à la broche 13.

## 2. Construis le programme

La zone de travail contient déjà le bloc **quand le programme démarre** (sinon : clique sur **Nouveau**,
puis **Projet vide**). Tout ce qui sera accroché dessous s'exécutera quand la carte démarre.

1. Dans la famille **Contrôle** (orange), glisse **répéter indéfiniment** sous *quand le programme démarre*.
   Il s'emboîte quand une ombre grise apparaît.
2. Dans la famille **Broches**, glisse **allumer la LED intégrée** *dans* la bouche de *répéter
   indéfiniment*.
3. Dans **Contrôle**, glisse **attendre 1 secondes** sous *allumer la LED intégrée*.
4. Ajoute encore **allumer la LED intégrée** en dessous, et clique sur sa petite flèche pour choisir
   **éteindre**.
5. Ajoute un dernier **attendre 1 secondes**.

Ton programme doit ressembler à ceci :

```
quand le programme démarre
répéter indéfiniment
    allumer la LED intégrée
    attendre 1 secondes
    éteindre la LED intégrée
    attendre 1 secondes
```

> **Pourquoi deux « attendre » ?** La carte est très rapide. Sans le second, elle rallumerait la LED
> une fraction de seconde après l'avoir éteinte : tu ne verrais jamais la LED éteinte.

## 3. Envoie-le dans la carte

1. Clique sur **Vérifier** (vert) : Scratch2Arduino contrôle ton programme. S'il manque quelque chose,
   la barre d'état en bas te dit quoi corriger et le bloc concerné est mis en évidence.
2. Clique sur **Téléverser** (orange). La première fois, c'est un peu long : les outils de la carte se
   téléchargent (il faut Internet).
3. La LED **L** clignote : une seconde allumée, une seconde éteinte. Bravo, c'est ton premier programme !

Débranche la carte, puis rebranche-la (ou alimente-la avec une pile) : le programme repart tout seul. Il
reste dans la carte jusqu'au prochain téléversement.

## 4. Essaie le mode Live

1. Clique sur **Live** (bleu). La première fois, un petit programme *S2A live* s'installe sur la carte.
2. Les blocs qui tournent s'éclairent. Change le premier **1** en **0.2** : la LED clignote plus vite,
   **tout de suite**, sans téléverser.
3. Clique sur **Arrêter** pour quitter le mode Live.

En mode Live, c'est l'ordinateur qui exécute les blocs et qui commande la carte : pratique pour essayer.
Pour que la carte travaille seule, il faut **téléverser**.

## 5. Enregistre ton projet

Clique sur **Enregistrer** (ou Ctrl+S), donne un nom au fichier : il est rangé dans
`Documents/Scratch2Arduino`. Clique sur **Voir le code** pour découvrir le code Arduino fabriqué à partir
de tes blocs.

## Défis

- Fais un clignotement très rapide (0.1 seconde), puis très lent.
- Fais un « battement de cœur » : deux flashs rapides, puis une pause.
- Écris **SOS** en morse : 3 flashs courts, 3 longs, 3 courts (utilise **répéter 3 fois**).

## Si ça ne marche pas

- *« Branche ta carte Arduino puis choisis son port avant de téléverser »* : choisis le port dans la barre du haut.
- La liste des ports reste vide : essaie un autre câble USB (certains ne font que recharger).
- Voir aussi [En cas de problème](../guide.md#15-en-cas-de-problème).

**Suivant** : [Tutoriel 2 — Feu tricolore](02-traffic-light.md)
