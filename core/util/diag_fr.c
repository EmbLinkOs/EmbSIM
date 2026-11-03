/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* diag_fr.c -- the engine's diagnostics in French.
 *
 * Each row gives an English format as the engine raises it (its code and
 * its printf format) and the French with the same conversions in the same
 * order. A raised message is matched against its code's English formats;
 * the text each conversion printed is carried into the French unchanged,
 * except the fixed phrases of the fragment rows (code "") a message is
 * built from. tools/i18n/diagnostics.py finds every format the engine
 * raises and fails when one has no row here or its conversions differ.
 *
 * Messages built elsewhere and passed through whole (a JSON parser's error,
 * a project validation's reason, a Lua model's own text) have no row and
 * stay in English. */
#include "util/diag.h"
#include <string.h>
#include "util/str.h"

typedef struct fr_row {
    const char *code, *en, *fr;
} fr_row;

static const fr_row rows[] = {
    /* the air: sniffers, access points, scanners, servers */
    { "air_sniffer.channel", "a BLE sniffer listens on advertising channel 37, 38 or 39",
      "un renifleur BLE écoute le canal d'annonce 37, 38 ou 39" },
    { "air_sniffer.channel", "a Wi-Fi sniffer listens on a channel from 1 to 14",
      "un renifleur Wi-Fi écoute un canal de 1 à 14" },
    { "air_sniffer.radio", "the radio is \"wifi\" or \"ble\"",
      "la radio est \"wifi\" ou \"ble\"" },
    { "air_sniffer.snaplen", "the snapshot length is from 16 to 2400 bytes",
      "la longueur de capture va de 16 à 2400 octets" },
    { "ble_central.address", "a random static address: its top two bits are 1",
      "une adresse statique aléatoire : ses deux bits de poids fort valent 1" },
    { "ble_central.interval", "the interval is 7.5 ms to 4 s and the timeout 100 ms to 32 s, more than twice it",
      "l'intervalle va de 7,5 ms à 4 s et le délai de supervision de 100 ms à 32 s, plus du double de l'intervalle" },
    { "ble_scanner.address", "a random static address: its top two bits are 1",
      "une adresse statique aléatoire : ses deux bits de poids fort valent 1" },
    { "ble_scanner.window", "the scan window is between 0 and the interval",
      "la fenêtre de balayage est comprise entre 0 et l'intervalle" },
    { "usb_host.address", "a device address is 1 to 127",
      "une adresse de périphérique va de 1 à 127" },
    { "usb_host.reset", "a port reset lasts at least 10 ms (USB 2.0 7.1.7.5)",
      "une réinitialisation de port dure au moins 10 ms (USB 2.0 7.1.7.5)" },
    { "web_server.body", "the body is at most %d bytes",
      "le corps fait au plus %d octets" },
    { "web_server.params", "an IPv4 address, a latency of 0 to 2000 ms and a port of 1 to 65535",
      "une adresse IPv4, une latence de 0 à 2000 ms et un port de 1 à 65535" },
    { "wifi_ap.channel", "the 2.4 GHz channels are 1 to 14",
      "les canaux à 2,4 GHz vont de 1 à 14" },
    { "wifi_ap.ip", "the router's address is a dotted IPv4 one",
      "l'adresse du routeur est une adresse IPv4 en notation pointée" },
    { "wifi_ap.password", "a WPA2 passphrase is 8 to 63 characters",
      "une phrase secrète WPA2 compte de 8 à 63 caractères" },
    { "wifi_ap.ssid", "an SSID is at most 32 bytes",
      "un SSID fait au plus 32 octets" },
    { "wifi_ap.wan", "a dotted IPv4 WAN address and a latency of 0 to 2000 ms",
      "une adresse WAN IPv4 en notation pointée et une latence de 0 à 2000 ms" },
    { "wifi_scanner.address", "an individual MAC address: its first octet is even",
      "une adresse MAC individuelle : son premier octet est pair" },
    { "wifi_scanner.period", "channels 1 to 14, and a period that holds every dwell",
      "des canaux de 1 à 14 et une période qui contient chaque temps d'écoute" },

    /* the analog engine */
    { "analog.implicit_ground", "no ground net; an implicit ground node was added",
      "aucun réseau de masse ; un nœud de masse implicite a été ajouté" },
    { "analog.implicit_supply", "pull-up to the session's default supply voltage",
      "rappel au niveau haut vers la tension d'alimentation par défaut de la session" },
    { "analog.no_convergence", "%s did not converge; worst residuals:",
      "%s n'a pas convergé ; pires résidus :" },
    { "analog.param_out_of_range", "parameter %s of %s is outside its declared range",
      "le paramètre %s de %s est hors de sa plage déclarée" },
    { "analog.param_out_of_range", "%s: %s", "%s : %s" },
    { "analog.singular_topology", "singular matrix at %s (devices: %s)",
      "matrice singulière en %s (composants : %s)" },
    { "analog.singular_topology", "two power pins with different voltages on one net; the first one is used",
      "deux broches d'alimentation de tensions différentes sur un même réseau ; la première est utilisée" },
    { "analog.step_failure", "substep bound exceeded",
      "nombre maximal de sous-pas dépassé" },
    { "analog.step_failure", "too many consecutive step rejections",
      "trop de pas rejetés à la suite" },
    { "analog.supply_driven", "the circuit has a supply of its own; the power pin's ideal source is not added",
      "le circuit a sa propre alimentation ; la source idéale de la broche d'alimentation n'est pas ajoutée" },
    { "analog.supply_driven", "the net is driven by a component model; the power pin's ideal source is not added",
      "le réseau est piloté par un modèle de composant ; la source idéale de la broche d'alimentation n'est pas ajoutée" },
    { "analog.timestep_too_small", "step below h_min at %s (largest error: %s)",
      "pas inférieur à h_min en %s (plus grande erreur : %s)" },

    /* the bench and the project */
    { "bench.invalid", "%s%s", "%s%s" },
    { "catalog.composite_depth", "%s nests composites deeper than 8 levels%s",
      "%s imbrique des composites sur plus de 8 niveaux%s" },
    { "catalog.file_missing", "%s names the script %s, which is not in its directory",
      "%s nomme le script %s, absent de son répertoire" },
    { "catalog.instantiate", "cannot instantiate %s: %s",
      "impossible d'instancier %s : %s" },
    { "catalog.model_unsupported", "%s binds unknown primitive %s",
      "%s utilise la primitive inconnue %s" },
    { "catalog.model_unsupported", "%s declares no model usable under this policy%s",
      "%s ne déclare aucun modèle utilisable avec cette politique%s" },
    { "catalog.model_unsupported", "%s is a composite and cannot be instantiated here%s",
      "%s est un composite et ne peut pas être instancié ici%s" },
    { "catalog.model_unsupported", "%s uses a native model, which this version does not load%s",
      "%s utilise un modèle natif, que cette version ne charge pas%s" },
    { "catalog.model_unsupported", "%s: %s", "%s : %s" },
    { "catalog.not_found", "MCU %s of board %s is not in any library",
      "le MCU %s de la carte %s n'est dans aucune bibliothèque" },
    { "catalog.not_found", "board %s is not in any library%s",
      "la carte %s n'est dans aucune bibliothèque%s" },
    { "catalog.not_found", "board component %s (%s) is not in any library",
      "le composant de carte %s (%s) n'est dans aucune bibliothèque" },
    { "catalog.not_found", "component %s is not in any library%s",
      "le composant %s n'est dans aucune bibliothèque%s" },
    { "catalog.not_found", "composite member %s (%s) is not in any library",
      "le membre de composite %s (%s) n'est dans aucune bibliothèque" },
    { "catalog.not_found", "library directory not found",
      "répertoire de bibliothèque introuvable" },
    { "catalog.not_found", "package %s of MCU %s not found",
      "boîtier %s du MCU %s introuvable" },
    { "catalog.pin_twice", "pin %s.%s appears in more than one net",
      "la broche %s.%s apparaît dans plus d'un réseau" },
    { "catalog.pin_unknown", "unknown pin %s in board net %s",
      "broche inconnue %s dans le réseau de carte %s" },
    { "catalog.pin_unknown", "unknown pin %s in composite net %s",
      "broche inconnue %s dans le réseau de composite %s" },
    { "catalog.pin_unknown", "unknown pin %s in net %s",
      "broche inconnue %s dans le réseau %s" },
    { "catalog.port_missing", "composite %s has no port for pin %s",
      "le composite %s n'a pas de port pour la broche %s" },
    { "input.rejected", "%s does not accept input %s",
      "%s n'accepte pas l'entrée %s" },
    { "io.read", "cannot read %s", "impossible de lire %s" },
    { "island.requires_electrical", "%s has no functional model",
      "%s n'a pas de modèle fonctionnel" },
    { "island.requires_electrical", "%s has no functional model (primitive %s is electrical)",
      "%s n'a pas de modèle fonctionnel (la primitive %s est électrique)" },
    { "probe.invalid", "probe %s: %s", "sonde %s : %s" },
    { "project.no_connect", "no_connect names %s, which is no pin of the project%s",
      "no_connect nomme %s, qui n'est aucune broche du projet%s" },
    { "project.no_connect_wired", "%s is in no_connect but is wired to %s",
      "%s figure dans no_connect mais est câblée à %s" },
    { "schema.faults", "%s%s", "%s%s" },
    { "schema.layout", "%s%s", "%s%s" },
    { "schema.missing_field", "a board entry needs id and board%s%s",
      "une entrée de carte demande id et board%s%s" },
    { "schema.missing_field", "id and kind are required",
      "id et kind sont obligatoires" },
    { "schema.missing_field", "parameter %s of %s is required",
      "le paramètre %s de %s est obligatoire" },
    { "schema.missing_field", "pin %u of %s needs a name and a model",
      "la broche %u de %s demande un nom et un modèle" },
    { "schema.missing_field", "the firmware of %s has no image%s",
      "le micrologiciel de %s n'a pas d'image%s" },
    { "schema.type", "component ids must be identifiers%s%s",
      "les id de composants doivent être des identifiants%s%s" },
    { "schema.type", "kind must be \"project\"%s%s",
      "kind doit valoir \"project\"%s%s" },
    { "schema.type", "nets is an object of net names to pin lists%s%s",
      "nets est un objet associant des noms de réseaux à des listes de broches%s%s" },
    { "schema.type", "parameter %s of %s is not one of its enum values",
      "le paramètre %s de %s ne fait pas partie de ses valeurs énumérées" },
    { "schema.type", "parameter %s of %s must be a boolean",
      "le paramètre %s de %s doit être un booléen" },
    { "schema.type", "parameter %s of %s must be a number",
      "le paramètre %s de %s doit être un nombre" },
    { "schema.type", "pin %s of %s has an invalid pin model",
      "la broche %s de %s a un modèle de broche invalide" },
    { "schema.type", "policy must be functional, timed or electrical%s%s",
      "policy doit valoir functional, timed ou electrical%s%s" },
    { "schema.unknown_field", "%s has no parameter named %s",
      "%s n'a pas de paramètre nommé %s" },
    { "schema.version", "unsupported schema_version; expected 1%s%s",
      "schema_version non prise en charge ; 1 attendu%s%s" },

    /* model extensions and injected faults */
    { "ext.budget_exceeded", "%s: %s (the model stops until the next reset)",
      "%s : %s (le modèle s'arrête jusqu'à la prochaine réinitialisation)" },
    { "ext.error", "%s: %s", "%s : %s" },
    { "ext.error", "%s: %s (the model stops until the next reset)",
      "%s : %s (le modèle s'arrête jusqu'à la prochaine réinitialisation)" },
    { "ext.error", "%s: no script was registered",
      "%s : aucun script n'a été enregistré" },
    { "ext.error", "%s: the script must return its model table",
      "%s : le script doit renvoyer sa table de modèle" },
    { "fault.injected", "%s: fault %s", "%s : défaut %s" },

    /* instruments and parts */
    { "instrument.fuse_blown", "%s: the A input's 11 A fuse (F2) has blown: replace it",
      "%s : le fusible 11 A (F2) de l'entrée A a fondu : remplacez-le" },
    { "instrument.fuse_blown", "%s: the mA/uA input's 440 mA fuse (F1) has blown: replace it",
      "%s : le fusible 440 mA (F1) de l'entrée mA/uA a fondu : remplacez-le" },
    { "part.damaged",
      "%s: its junction passed %.0f C, its absolute maximum, at %.0f mW: the LED has burnt out (open); replace it (its replace input) after limiting its current",
      "%s : sa jonction a dépassé %.0f C, son maximum absolu, à %.0f mW : la LED a grillé (circuit ouvert) ; remplacez-la (son entrée replace) après avoir limité son courant" },
    { "part.over_rating",
      "%s: %.1f mA forward, above its %.0f mA DC rating: it heats (%.0f mW) and dims with age; a series resistor limits it",
      "%s : %.1f mA en direct, au-dessus de son courant continu nominal de %.0f mA : elle chauffe (%.0f mW) et faiblit en vieillissant ; une résistance en série le limite" },
    { "part.over_rating", "%s: %.2f V reverse, above its %.0f V rating",
      "%s : %.2f V en inverse, au-dessus de sa tension nominale de %.0f V" },
    { "part.thermal_shutdown",
      "%s: its die reached %.0f C dissipating %.2f W: thermal shutdown limits its output; it needs a heatsink, less input voltage or less load",
      "%s : sa puce a atteint %.0f C en dissipant %.2f W : la protection thermique limite sa sortie ; il lui faut un dissipateur, moins de tension d'entrée ou moins de charge" },
    { "pin.over_current",
      "%.1f mA %s the pin, above its %.0f mA absolute maximum DC current: the datasheet warns of permanent damage; limit it with a resistor or a driver",
      "%.1f mA %s la broche, au-dessus de son courant continu maximal absolu de %.0f mA : la fiche technique avertit de dommages permanents ; limitez-le avec une résistance ou un étage de commande" },
    { "pin.over_voltage",
      "%.2f V on the pin, outside its absolute maximum of %.1f to %.1f V: its protection diodes carry %.1f mA and the datasheet warns of permanent damage; limit the current with a resistor",
      "%.2f V sur la broche, hors de son maximum absolu de %.1f à %.1f V : ses diodes de protection conduisent %.1f mA et la fiche technique avertit de dommages permanents ; limitez le courant avec une résistance" },

    /* the microcontrollers */
    { "mcu.boot_mode",
      "BOOT0 is high: the part boots its system memory (the vendor's bootloader), which is not simulated; the core stays idle%s%s",
      "BOOT0 est à l'état haut : la puce démarre sur sa mémoire système (le chargeur d'amorçage du fabricant), qui n'est pas simulée ; le cœur reste inactif%s%s" },
    { "mcu.bus_error", "%s at 0x%08lx (pc 0x%08lx): %s has no register at +0x%03lx",
      "%s à 0x%08lx (pc 0x%08lx) : %s n'a pas de registre en +0x%03lx" },
    { "mcu.bus_error", "%s at 0x%08lx (pc 0x%08lx): no peripheral of %s is there",
      "%s à 0x%08lx (pc 0x%08lx) : aucun périphérique de %s à cette adresse" },
    { "mcu.clock_gated", "%s at pc 0x%08lx: %s.%s %s while its clock is off (%s is 0)",
      "%s à pc 0x%08lx : %s.%s %s tant que son horloge est coupée (%s vaut 0)" },
    { "mcu.clock_out_of_spec", "PCLK1 is %lu Hz, above APB1's %lu Hz",
      "PCLK1 vaut %lu Hz, au-dessus des %lu Hz d'APB1" },
    { "mcu.clock_out_of_spec", "PCLK2 is %lu Hz, above APB2's %lu Hz",
      "PCLK2 vaut %lu Hz, au-dessus des %lu Hz d'APB2" },
    { "mcu.clock_out_of_spec", "SYSCLK is %lu Hz, above the %lu Hz the voltage scale and over-drive allow",
      "SYSCLK vaut %lu Hz, au-dessus des %lu Hz que permettent l'échelle de tension et l'over-drive" },
    { "mcu.clock_out_of_spec", "the PLL's VCO input is %lu Hz: RM0390 asks 1 to 2 MHz (PLLM %lu)",
      "l'entrée du VCO de la PLL est à %lu Hz : RM0390 demande 1 à 2 MHz (PLLM %lu)" },
    { "mcu.clock_out_of_spec", "the PLL's VCO runs at %lu Hz: RM0390 asks 100 to 432 MHz (PLLN %lu)",
      "le VCO de la PLL tourne à %lu Hz : RM0390 demande 100 à 432 MHz (PLLN %lu)" },
    { "mcu.easydma_range",
      "%s's %s 0x%08lx is not in Data RAM: EasyDMA reaches only that (the part may fault or corrupt RAM); nothing is moved",
      "%s : %s 0x%08lx n'est pas en Data RAM : EasyDMA n'atteint qu'elle (la puce peut faire une faute ou corrompre la RAM) ; rien n'est déplacé" },
    { "mcu.easydma_range", "%s's SEQ[%d].PTR 0x%08lx is not in Data RAM: EasyDMA reaches only that; nothing is played",
      "%s : SEQ[%d].PTR 0x%08lx n'est pas en Data RAM : EasyDMA n'atteint qu'elle ; rien n'est joué" },
    { "mcu.easydma_range", "SAADC's RESULT.PTR 0x%08lx is not in Data RAM: EasyDMA reaches only that; nothing is stored",
      "RESULT.PTR du SAADC 0x%08lx n'est pas en Data RAM : EasyDMA n'atteint qu'elle ; rien n'est enregistré" },
    { "mcu.exit", "the firmware exited with status %s (%s)",
      "le micrologiciel s'est terminé avec le code %s (%s)" },
    { "mcu.flash_wait_states", "HCLK is %lu Hz with %lu flash wait states: the part would fetch wrong instructions",
      "HCLK vaut %lu Hz avec %lu états d'attente de la flash : la puce lirait des instructions erronées" },
    { "mcu.held_in_reset", "%s at pc 0x%08lx: %s.%s %s while it is held in reset (%s is 1)",
      "%s à pc 0x%08lx : %s.%s %s tant qu'il est maintenu en réinitialisation (%s vaut 1)" },
    { "mcu.image", "%s: %s", "%s : %s" },
    { "mcu.irq_storm",
      "%s: %s (exception %d) entered %lu times in %s%s: it is taken again as soon as its handler returns, so what raises it stays set (a flag the handler does not clear, a level-triggered input held active)",
      "%s : %s (exception %d) prise %lu fois en %s%s : elle est reprise dès que son gestionnaire rend la main, donc ce qui la déclenche reste actif (un drapeau que le gestionnaire n'efface pas, une entrée sensible au niveau maintenue active)" },
    { "mcu.locked_write", "a write at pc 0x%06lx to OSCCON's %s byte is ignored: it needs its unlock sequence first",
      "une écriture à pc 0x%06lx dans l'octet %s d'OSCCON est ignorée : il faut d'abord sa séquence de déverrouillage" },
    { "mcu.locked_write",
      "a write at pc 0x%08lx to OSCCON is ignored: it is locked (SYSKEY's unlock sequence was not written)",
      "une écriture à pc 0x%08lx dans OSCCON est ignorée : il est verrouillé (la séquence de déverrouillage de SYSKEY n'a pas été écrite)" },
    { "mcu.lockup", "the core locked up: %s%s", "le cœur s'est bloqué (lockup) : %s%s" },
    { "mcu.low_power_reset", "the option bytes' nRST_STDBY is 0: entering Standby resets the part",
      "nRST_STDBY des octets d'option vaut 0 : entrer en Standby réinitialise la puce" },
    { "mcu.low_power_reset", "the option bytes' nRST_STOP is 0: entering Stop resets the part",
      "nRST_STOP des octets d'option vaut 0 : entrer en Stop réinitialise la puce" },
    { "mcu.memory_file", "%s: %s cannot be read", "%s : impossible de lire %s" },
    { "mcu.memory_file", "%s: %s is not the file the description names (sha256 %s)",
      "%s : %s n'est pas le fichier que nomme la description (sha256 %s)" },
    { "mcu.model_missing", "no behavioural model yet for %s: those peripherals are plain registers",
      "pas encore de modèle de comportement pour %s : ces périphériques sont de simples registres" },
    { "mcu.no_boot2",
      "the RP2040's bootrom finds no boot2 in flash (the CRC-32 of its first 252 bytes is 0x%08lx, the word after them 0x%08lx): the part waits in its USB boot",
      "la ROM d'amorçage du RP2040 ne trouve pas de boot2 en flash (le CRC-32 de ses 252 premiers octets vaut 0x%08lx, le mot suivant 0x%08lx) : la puce attend dans son amorçage USB" },
    { "mcu.no_firmware", "%s has no firmware: its MCU's pins stay inputs, as a blank part's do",
      "%s n'a pas de micrologiciel : les broches de son MCU restent des entrées, comme celles d'une puce vierge" },
    { "mcu.no_firmware", "input %s to %s needs firmware on the board's MCU",
      "l'entrée %s vers %s demande un micrologiciel sur le MCU de la carte" },
    { "mcu.no_image", "%s: the firmware image %s (%s) cannot be read; build the firmware (embsim build) to run it",
      "%s : impossible de lire l'image du micrologiciel %s (%s) ; compilez le micrologiciel (embsim build) pour l'exécuter" },
    { "mcu.register_file", "%s: register files of format %s are converted first (tools/importers/svd2regs.py)",
      "%s : les fichiers de registres au format %s se convertissent d'abord (tools/importers/svd2regs.py)" },
    { "mcu.register_file", "%s: the register description %s cannot be read",
      "%s : impossible de lire la description de registres %s" },
    { "mcu.rescue", "the RP2040 was reset by its Rescue DP: its bootrom halts in its dead loop for the debugger",
      "le RP2040 a été réinitialisé par son Rescue DP : sa ROM d'amorçage s'arrête dans sa boucle morte pour le débogueur" },
    { "mcu.rww_read",
      "LPM read 0x%04x in the RWW section while it is blocked (RWWSB): the result is undefined; set RWWSRE first",
      "lecture LPM en 0x%04x dans la section RWW alors qu'elle est bloquée (RWWSB) : le résultat est indéfini ; positionnez d'abord RWWSRE" },
    { "mcu.stale_image",
      "%s: the firmware image %s is older than its source %s (%.0f s newer); rebuild it (embsim build, or embsim run --build) or the run shows the old program",
      "%s : l'image du micrologiciel %s est plus ancienne que sa source %s (plus récente de %.0f s) ; recompilez-la (embsim build, ou embsim run --build) sinon l'exécution montre l'ancien programme" },
    { "mcu.uf2_written", "the RP2040's USB boot wrote all %lu blocks of a UF2 file to flash: it reboots",
      "l'amorçage USB du RP2040 a écrit les %lu blocs d'un fichier UF2 en flash : il redémarre" },
    { "mcu.unsupported_access", "%s has no behavioural model: its registers only keep what is written (pc 0x%08lx)",
      "%s n'a pas de modèle de comportement : ses registres ne font que garder ce qui y est écrit (pc 0x%08lx)" },
    { "mcu.usb_boot", "BOOTSEL is held as the RP2040's bootrom starts: the part waits in its USB boot, a drive named RPI-RP2",
      "BOOTSEL est maintenu au démarrage de la ROM d'amorçage du RP2040 : la puce attend dans son amorçage USB, un lecteur nommé RPI-RP2" },
    { "mcu.usb_boot", "the program called the RP2040's reset_to_usb_boot: the part waits in its USB boot",
      "le programme a appelé reset_to_usb_boot du RP2040 : la puce attend dans son amorçage USB" },
    { "mcu.usb_fifo",
      "OTG_FS: IN endpoint %d's FIFO (words %lu to %lu) overlaps the receive FIFO or runs past the core's 320 words of FIFO RAM",
      "OTG_FS : la FIFO du point de terminaison IN %d (mots %lu à %lu) chevauche la FIFO de réception ou dépasse les 320 mots de RAM FIFO du cœur" },
    { "mcu.usb_fifo", "OTG_FS: the receive FIFO is read with no packet's data taken (GRXSTSP first)",
      "OTG_FS : la FIFO de réception est lue sans qu'aucune donnée de paquet n'ait été prise (GRXSTSP d'abord)" },
    { "mcu.watchdog_reset", "the always-on watchdog's comparator fired with wdogrsten set: it resets the chip",
      "le comparateur du chien de garde toujours alimenté s'est déclenché avec wdogrsten positionné : il réinitialise la puce" },
    { "mcu.watchdog_reset", "the independent watchdog counted down to 0: it resets the part",
      "le chien de garde indépendant a décompté jusqu'à 0 : il réinitialise la puce" },
    { "mcu.watchdog_reset", "the watchdog timed out in system reset mode: it resets the part",
      "le chien de garde a expiré en mode réinitialisation système : il réinitialise la puce" },
    { "mcu.watchdog_reset", "the watchdog's CTRL.TRIGGER resets the chip",
      "CTRL.TRIGGER du chien de garde réinitialise la puce" },
    { "mcu.watchdog_reset", "the watchdog's counter reached 0: it resets the chip",
      "le compteur du chien de garde a atteint 0 : il réinitialise la puce" },
    { "mcu.watchdog_reset", "the window watchdog resets the part",
      "le chien de garde à fenêtre réinitialise la puce" },

    /* nets, the scheduler and performance */
    { "net.contention", "drivers %s.%s and %s.%s disagree at the strongest active drive strength",
      "les pilotes %s.%s et %s.%s sont en désaccord à la plus forte intensité de commande active" },
    { "net.floating_input", "%s.%s sampled net %s with no driver and no pull",
      "%s.%s a échantillonné le réseau %s sans pilote ni rappel" },
    { "net.unconnected_required_pin", "%s is not connected", "%s n'est pas connectée" },
    { "net.unknown_input", "%s.%s sampled X on net %s", "%s.%s a échantillonné X sur le réseau %s" },
    { "perf.hot_net",
      "%s takes %.0f%% of the analog steps: each of its %llu edges (%.4g Hz) re-solves its %u-node island of %s (embsim run --circuit-profile shows the rest)",
      "%s prend %.0f%% des pas analogiques : chacun de ses %llu fronts (%.4g Hz) résout à nouveau son îlot de %u nœuds de %s (embsim run --circuit-profile montre le reste)" },
    { "perf.island_size",
      "%s joins the %u-node island of %s through its pull-up: each of its %llu edges costs a solve of the whole island (emblink/pull_up to the rail's voltage keeps the net on its own, with the same levels)",
      "%s rejoint l'îlot de %u nœuds de %s par son rappel au niveau haut : chacun de ses %llu fronts coûte une résolution de tout l'îlot (emblink/pull_up à la tension du rail garde le réseau isolé, avec les mêmes niveaux)" },
    { "sched.delta_overflow", "zero-delay propagation exceeded %u delta cycles",
      "la propagation à délai nul a dépassé %u cycles delta" },
    { "sched.queue_full", "a pin change cannot be scheduled",
      "un changement de broche ne peut pas être planifié" },
    { "sched.queue_full", "cannot schedule a pin change",
      "impossible de planifier un changement de broche" },
    { "sched.queue_full", "the MCU's next quantum cannot be scheduled",
      "le prochain quantum du MCU ne peut pas être planifié" },
    { "sched.time_overflow", "delay overflows the time horizon",
      "le délai dépasse l'horizon temporel" },

    /* USB */
    { "usb.dfu", "the DFU download failed: %s", "le téléchargement DFU a échoué : %s" },
    { "usb.enumeration", "the device did not enumerate: %s failed (%s)",
      "le périphérique ne s'est pas énuméré : %s a échoué (%s)" },

    /* fragments a message is built from */
    { "", "a read", "une lecture" },
    { "", "a write", "une écriture" },
    { "", "an access", "un accès" },
    { "", "a byte read", "une lecture d'octet" },
    { "", "a halfword read", "une lecture de demi-mot" },
    { "", "a word read", "une lecture de mot" },
    { "", "a byte write", "une écriture d'octet" },
    { "", "a halfword write", "une écriture de demi-mot" },
    { "", "a word write", "une écriture de mot" },
    { "", "an exception", "une exception" },
    { "", "cleared", "levé" },
    { "", "injected", "injecté" },
    { "", "error object is not a string", "l'objet d'erreur n'est pas une chaîne" },
    { "", "is ignored", "ignore l'écriture" },
    { "", "reads as 0", "se lit 0" },
    { "", "reads its reset value", "se lit à sa valeur de réinitialisation" },
    { "", "into", "entrant dans" },
    { "", "out of", "sortant de" },
    { "", "the option bytes' nRST_STDBY is 0: entering Standby resets the part",
      "nRST_STDBY des octets d'option vaut 0 : entrer en Standby réinitialise la puce" },
    { "", "the option bytes' nRST_STOP is 0: entering Stop resets the part",
      "nRST_STOP des octets d'option vaut 0 : entrer en Stop réinitialise la puce" },
    { "", "the watchdog's CTRL.TRIGGER resets the chip", "CTRL.TRIGGER du chien de garde réinitialise la puce" },
    { "", "the watchdog's counter reached 0: it resets the chip",
      "le compteur du chien de garde a atteint 0 : il réinitialise la puce" },
};

#define NROWS (sizeof rows / sizeof rows[0])
#define MAXCAPS 12

typedef struct span {
    const char *s;
    size_t n;
} span;

/* the length of the conversion at f (f[0] is '%'); 0 when it is none */
static size_t conv_len(const char *f)
{
    size_t i = 1;
    if (f[1] == '%')
        return 2;
    while (f[i] && strchr("-+ #0", f[i]))
        i++;
    while (f[i] == '*' || (f[i] >= '0' && f[i] <= '9'))
        i++;
    if (f[i] == '.') {
        i++;
        while (f[i] == '*' || (f[i] >= '0' && f[i] <= '9'))
            i++;
    }
    while (f[i] && strchr("hlzjtL", f[i]))
        i++;
    return f[i] && strchr("diouxXeEfgGcspa", f[i]) ? i + 1 : 0;
}

/* does message m match format f? Each conversion takes the shortest text that
 * lets the rest match. With prefix 1, f need only match m's beginning; with 2,
 * m may end before f (a message cut at its record's length); *end is where
 * the match stops. */
static int match(const char *f, const char *m, span *caps, int n, int prefix, const char **end, long *budget)
{
    if (--*budget < 0)
        return 0; /* too many ways to split the message: give it up, it stays English */
    while (*f) {
        if (*f == '%') {
            size_t k = conv_len(f);
            if (f[1] == '%') {
                if (*m != '%')
                    return 0;
                f += 2;
                m++;
                continue;
            }
            if (k) {
                const char *t;
                if (n >= MAXCAPS)
                    return 0;
                for (t = m;; t++) {
                    caps[n].s = m;
                    caps[n].n = (size_t)(t - m);
                    if (match(f + k, t, caps, n + 1, prefix, end, budget))
                        return 1;
                    if (!*t)
                        return 0;
                }
            }
        }
        if (!*m && prefix == 2) {
            /* the message was cut at the record's length: what is left of the
             * format printed nothing */
            for (; *f; f++)
                if (*f == '%' && f[1] != '%' && conv_len(f) && n < MAXCAPS) {
                    caps[n].s = m;
                    caps[n++].n = 0;
                }
            *end = m;
            return 1;
        }
        if (*f != *m)
            return 0;
        f++;
        m++;
    }
    if (*m && !prefix)
        return 0;
    *end = m;
    return 1;
}

/* a row whose English has no word of its own ("%s: %s"): it carries messages
 * built elsewhere, which it cannot translate */
static int generic(const char *en)
{
    const char *p = en;
    while (*p) {
        if (*p == '%') {
            size_t k = conv_len(p);
            p += k ? k : 1;
            continue;
        }
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
            return 0;
        p++;
    }
    return 1;
}

typedef struct out_buf {
    char *s;
    size_t cap, len;
} out_buf;

static void put(out_buf *o, const char *s, size_t n)
{
    size_t room = o->len + 1 < o->cap ? o->cap - o->len - 1 : 0;
    if (n > room)
        n = room;
    memcpy(o->s + o->len, s, n);
    o->len += n;
    o->s[o->len] = 0;
}

static const char *fragment(const span *c)
{
    size_t i;
    for (i = 0; i < NROWS; i++)
        if (!rows[i].code[0] && strlen(rows[i].en) == c->n && !memcmp(rows[i].en, c->s, c->n))
            return rows[i].fr;
    return NULL;
}

static void render(out_buf *o, const char *fr, const span *caps)
{
    int n = 0;
    while (*fr) {
        const char *p = strchr(fr, '%');
        size_t k;
        if (!p) {
            put(o, fr, strlen(fr));
            return;
        }
        put(o, fr, (size_t)(p - fr));
        k = conv_len(p);
        if (p[1] == '%') {
            put(o, "%", 1);
        } else if (k) {
            const char *t = fragment(&caps[n]);
            if (t)
                put(o, t, strlen(t));
            else
                put(o, caps[n].s, caps[n].n);
            n++;
        } else {
            put(o, "%", 1);
            k = 1;
        }
        fr = p + k;
    }
}

/* does every stretch of literal text in format f (up to a conversion) occur
 * in message m? A cheap test that rules most rows out before matching. */
static int literals_in(const char *f, const char *m)
{
    char chunk[64];
    size_t n = 0;
    for (;; f++) {
        size_t k = *f == '%' ? conv_len(f) : 0;
        if (!*f || (k && f[1] != '%')) {
            chunk[n] = 0;
            if (n >= 3 && !strstr(m, chunk))
                return 0;
            n = 0;
            if (!*f)
                return 1;
            f += k - 1;
            continue;
        }
        if (n + 1 < sizeof chunk)
            chunk[n++] = *f;
        if (k == 2 && f[1] == '%')
            f++;
    }
}

static int translate(const char *code, const char *msg, int cut, out_buf *o)
{
    span caps[MAXCAPS];
    const char *end;
    size_t i;
    int prefix;
    long budget;
    for (prefix = 0; prefix < (cut ? 3 : 2); prefix++)
        for (i = 0; i < NROWS; i++) {
            if (!rows[i].code[0] || strcmp(rows[i].code, code) || generic(rows[i].en))
                continue;
            /* a cut message may have lost the format's later text */
            if (prefix < 2 && !literals_in(rows[i].en, msg))
                continue;
            budget = 4096;
            if (match(rows[i].en, msg, caps, 0, prefix, &end, &budget)) {
                render(o, rows[i].fr, caps);
                put(o, end, strlen(end)); /* what was appended after the format */
                return 1;
            }
        }
    return 0;
}

int esk_diag_french(const char *code, const char *subject, const char *message, char *out, size_t cap)
{
    out_buf o;
    size_t sl = subject ? strlen(subject) : 0;
    int cut = message && strlen(message) + 1 >= sizeof ((esk_diag_record *)0)->message;
    if (!code || !message || !out || !cap)
        return 0;
    o.s = out;
    o.cap = cap;
    o.len = 0;
    out[0] = 0;
    /* a component's message the MCU runtime prefixed with the component's id */
    if (sl && !strncmp(message, subject, sl) && message[sl] == ':' && message[sl + 1] == ' ') {
        put(&o, subject, sl);
        put(&o, " : ", 3);
        if (translate(code, message + sl + 2, cut, &o))
            return 1;
        o.len = 0;
        out[0] = 0;
    }
    return translate(code, message, cut, &o);
}
