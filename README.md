# Description

openqm_httpd_server est un serveur web http autonome et léger. Ce serveur permet de répondre à tous les types de requête web et de renvoyer une réponse composée d'une en-tête, d'un statut httpd et d'un contenu correspondant au Content-Type. Le contenu de la réponse sera le plus souvent sous format texte. Bien qu'OpenQM supporte la gestion des flux binaires on évitera de gérer ce type de contenu qui sera beaucoup mieux pris en charge par d'autres technologies. Chaque requête valide est prise en charge par une routine. Cette routine reçoit toutes les informations en entrée, l'url, la méthode, les données en paramètre GET ou POST, les données d'en-tête, l'adresse du client et certaines informations du serveur. Contrairement à mon premier essai avec openqm_https_mod, un module Apache httpd, ce serveur prend en charge :
- La personnalisation de la réponse lorsqu'il y a un statut d'erreur. C'est interessant dans le cadre d'une API lorsque l'on doit par exemple valider un formulaire et que l'on souhaite renvoyer un message d'erreur contenant un code numérique, un message et le champ auquel se rapporte l'erreur. Dans ce cas on peut par exemple utiliser le statut 400 (bad request) avec un retour json/xml contenant ces informations.
- La requête ne contient pas le nom de la routine OpenQM à appeler, ainsi on dévoile moins d'informations techniques. C'est un fichier de configuration qui permet de faire la correspondance entre le couple url/méthode et la routine à appeler.

Ce serveur n'a pas pour vocation à être appelé directement depuis le web, au lieu de ça il est préférable que les requêtes arrivent à un serveur plus complet comme Apache httpd. Ce serveur Apache httpd va ensuite renvoyer les requêtes au serveur openqm_httpd_server en proxy reverse. L'avantage de cette configuration c'est que l'on va bénéficier :
- D'un serveur léger et flexible pour répondre aux requêtes (openqm_httpd_server).
- D'un serveur robuste, configurable et extensible pour répondre de façon sécurisée aux requêtes provenant de l'extérieur (Apache httpd ou autre). C'est à ce serveur de prendre en charge le chiffrage de la connexion via le https et les normes liées.
- D'avoir un seul nom de host pour contenir d'une part l'application web (page html statique, application basée sur un framework comme ReactOS ou VueJS, les ressources comme les css, les images et les polices de caractères) et d'autres part l'API qui va interagir avec notre base de données nosql OpenQM/ScarletDME.
   
# Installation

## Dépendances
- libmicrohttpd12
- libmicrohttpd_dev
- libconfig9
- libconfig_dev

# Utilisation

Le serveur est un programme exécutable qui est paramétré via un fichier de configuration. Une fois le programme exécuté en fantôme il va répondre aux requêtes web qu'il reçoit et appelant une routine lorsque l'uri correspond à l'une de celles paramétrées.

## Configuration

Le fichier de configuration a une syntaxe [libconfig](http://hyperrealm.github.io/libconfig/). Au premier niveau le fichier de configuration doit contenir :

### httpd

Permet de définir les paramètres du serveur. Il est composé de :
- port = Numéro du port auquel le serveur répond.
- env : Un tableau qui contient les variables d'environnement du serveur. Par exemple QMCONFIG = le chemin et le nom du fichier de configuration OpenQM alternatif à /etc/openqm.conf.

### openqm

Permet de définir les paramètres d'OpenQM. Il est composé de l'unique variable :
- account = Nom du compte OpenQM dans lequel sont cataloguées les routines.

### url

Permet de définir les url valides, les contrôles à réaliser et la routine à appeler. Il s'agit d'un tableau d'objets. Chaque objet est un chemin d'url (un seul niveau à la fois) et contient :
- path = Nom d'un niveau de sous-répertoire dans l'url.
- sub_path : Est un tableau d'objets de même nature que celui-ci pour le niveau suivant de sous-répertoire.
- pattern = Une expression régulière qui permet de valider un sous-répertoire à la place de path.
- subr = Nom de la routine à appeler.
- method = Un tableau de chaînes indiquant les méthodes http utilisables pour appeler la routine.
- get_param = Un tableau de chaînes indiquant la liste des paramètres acceptés lors d'un appel GET.

## Routines

# TODO

Dans la configuration ajouter un post_param pour limiter les paramètres post acceptés.

Gérer la réception de fichiers via une méthode post et écrire le fichier reçu dans un répertoire temporaire avec un nom de fichier temporaire et aléatoire.
