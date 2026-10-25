# Amal Studio

**Amal Studio** est un studio de diffusion en direct et d'enregistrement pour les créateurs de contenus musulmans : conférences, tafsir, cours, concours de récitation.

- Le Coran entier, sourate par sourate et verset par verset (texte Tanzil, non modifié).
- Sept recueils de hadiths en arabe, avec les degrés indiqués par la source.
- Habillages, thèmes, cadres d'événement, fonds pour les événements religieux, intros vidéo.
- Plusieurs intervenants à l'écran, caméra du téléphone avec son micro, relecture au ralenti.
- Diffusion sur plusieurs plateformes à la fois, assistant de démarrage et assistant micro.
- Interface en français, pour Windows 10 et 11 (64 bits).

Amal Studio est basé sur [OBS Studio](https://obsproject.com). Il n'est ni affilié au projet OBS, ni approuvé par lui.

## Essai et licence Pro

Amal Studio s'installe gratuitement. Les fonctions de base restent gratuites pour toujours : diffusion et enregistrement, Coran, hadiths, programme du direct, habillages de base, assistants, caméra du téléphone. Les fonctions **Pro** (multidiffusion, cadres d'événement, fonds d'événements religieux, intros vidéo, relecture au ralenti et transitions Pro) sont disponibles pendant **30 jours d'essai**, puis avec une licence annuelle (25 000 FCFA, mises à jour comprises) : menu **Amal > Licence Amal Studio Pro**, contact WhatsApp +223 93 27 08 98.

La licence Pro est un service de l'éditeur : le code source reste publié sous GPL (voir plus bas).

## Télécharger

Les versions sont publiées dans la page [Releases](https://github.com/amal-studio/amal-studio/releases) : installateur (`Amal-Studio-…-Installer-x64.exe`) et version portable (`.zip`). Le guide d'utilisation en PDF est fourni avec chaque version (menu **Amal > Guide d'utilisation**).

La désinstallation se fait depuis **Paramètres Windows > Applications** ou le raccourci « Désinstaller Amal Studio » du menu Démarrer ; elle conserve vos projets.

## Compiler depuis le code source

Chaque version est compilée par GitHub Actions avec le fichier [.github/workflows/amal-windows.yaml](.github/workflows/amal-windows.yaml) : Windows Server 2025, Visual Studio 2026, script `.github/scripts/Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo`, puis l'installateur NSIS `build-aux/amal/installer/amal-studio.nsi`. Les dépendances précompilées sont celles d'OBS (obs-deps 2026-07-15, Qt 6, CEF). Les modules propres à Amal Studio se trouvent dans `plugins/amal-*`.

## Contact

Mainteneur : Amal Ben Aly (Bamako, Mali) — alybako.cm@gmail.com — ou les [issues GitHub](https://github.com/amal-studio/amal-studio/issues).

## Licence

Amal Studio est un logiciel libre distribué sous la licence publique générale GNU (GPL) version 2 ou ultérieure ; l'ensemble distribué, qui inclut FFmpeg compilé avec x264, est régi par la GPL version 3. L'inventaire des composants et de leurs licences figure dans [build-aux/amal/LICENCES-TIERS.txt](build-aux/amal/LICENCES-TIERS.txt).

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).
*(Signature gratuite fournie par SignPath.io, certificat de la SignPath Foundation — sous réserve d'acceptation du projet.)*

Team roles / Rôles de l'équipe :

- Committers and reviewers / Auteurs et relecteurs : [membres de l'organisation amal-studio](https://github.com/orgs/amal-studio/people)
- Approvers / Approbateurs : [propriétaires de l'organisation amal-studio](https://github.com/orgs/amal-studio/people?query=role%3Aowner)

Every signed release is built automatically from this repository by GitHub Actions ([.github/workflows/amal-windows.yaml](.github/workflows/amal-windows.yaml)) and approved manually before signing.
*(Chaque version signée est compilée automatiquement à partir de ce dépôt, puis approuvée manuellement avant la signature.)*

## Privacy policy / Confidentialité

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.

*Amal Studio n'envoie aucune information à d'autres systèmes en réseau sans la demande explicite de l'utilisateur ou de la personne qui l'installe ou l'utilise* : les seules connexions sont celles que vous lancez (diffusion vers les plateformes que vous configurez, caméra du téléphone sur votre réseau local, mise en ligne du journal à votre demande). Les clés de stream restent sur votre ordinateur.

## Code source d'OBS Studio

Le fichier [README.rst](README.rst) d'origine du projet OBS Studio est conservé.
