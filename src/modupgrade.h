// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:02): "Der normale Vanilla-Beton-MG-Bunker (der weisse Standardbunker) ist das Upgrade (wird wie ein Modul
// verwendet) fuer den Scavenger-Bunker. Muss aber erforscht werden."
//
// stats/structupgrades.json (Mod-Paket, bunkermine.py): { "Ausbau": "Grundbauwerk", ... }. Ein Ausbau darf nur genau auf ein eigenes,
// fertiges Grundbauwerk gesetzt werden (Bauvorschau, Bauauftrag, KI) und ersetzt es beim Baubeginn - wie ein Modul auf der Fabrik.
// Alles laeuft ueber gewoehnliche Bauauftraege und Spielzeit (Mehrspieler-synchron).
#pragma once

struct STRUCTURE;
struct STRUCTURE_STATS;

namespace modupgrade
{
/// ist das ein Ausbau (darf nur auf sein Grundbauwerk)?
bool isUpgrade(const STRUCTURE_STATS *psStats);
/// darf psNew (Ausbau) an die Stelle von psOld gebaut werden? (eigenes, fertiges Grundbauwerk)
bool canReplace(const STRUCTURE_STATS *psNew, const STRUCTURE *psOld, unsigned player);
/// (Wunsch 18.11. 12:48 "Modul auf Scav-Bunker bauen soll genauso sein wie beim Kraftwerksmodul"): der fuer diesen Spieler
/// verfuegbare Ausbau, der auf psOld gesetzt werden kann (nullptr = keiner) - fuer Bau-Zeiger beim Ueberfahren und Klick-Befehl
STRUCTURE_STATS *upgradeFor(const STRUCTURE *psOld, unsigned player);
}
