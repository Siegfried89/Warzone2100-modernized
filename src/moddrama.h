// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkt 5 "Rufe neu, dramatisch, draussen auf dem Feld" - ganz am Schluss; 17.11. 22:57 "alles abarbeiten"):
// ZUSAETZLICH zu den bisherigen Schlachtrufen (modvoice) erklingen dramatische Rufe, sobald irgendwo ein Gefecht beginnt (mehrere Maenner
// rufen durcheinander: "Charge!", "Here they come - open fire!") und waehrend es laeuft (alle 7-14 s ein Ruf: "Hold the line!", "Fall back!").
// Entfernung hoerbar, nicht nur leiser: nahe Gefechte (bis 10 Felder von der Kamera) klingen wie ein Ruf neben einem, ferne (bis 40 Felder)
// wie "da unten auf dem Feld" (eigene Fern-Fassung: duenn, hallend, mit Echo) aus der Richtung des Gefechts.
// Nur Gefechte, die man sehen kann oder an denen eigene Einheiten beteiligt sind, und nur in der Ebene, die man gerade ansieht.
// Reine Tonausgabe: liest nur, aendert nichts am Spiel, eigener Zufall (kein gameRand). Fehlen die Dateien (drama_*.ogg), bleibt es still.
#pragma once

struct BASE_OBJECT;
struct PROJECTILE;

namespace moddrama
{
/// ein Objekt wurde getroffen (Einheit oder Gebaeude)
void onHit(const BASE_OBJECT *victim, const PROJECTILE *psProj);
/// jeden Spielschritt (rechnet selbst nur alle 500 ms)
void tick();
/// neues Spiel / Laden
void reset();
}
