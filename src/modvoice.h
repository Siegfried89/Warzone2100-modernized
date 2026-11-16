// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schlachtrufe und Funkspruch im Gefecht (nach Sudden Strike / Blitzkrieg). Einheiten rufen im Kampf, bestaetigen Befehle,
// Panzer melden sich im Funkdeutsch ("Moving into range"), Transporter melden Beschuss, bei Artilleriebeschuss gibt es eigene Rufe.
// Reine Anzeige/Ton: beeinflusst den Spielzustand nicht und benutzt deshalb weder gameRand noch Zustand der Simulation.
// Die Sprachdateien (audio/battle/*.ogg) kommen aus dem Mod-Paket; fehlen sie, bleibt es still.
#pragma once

struct DROID;
struct GameWorld;
struct PROJECTILE;

namespace modvoice
{
/// Einheit wurde von einem Geschoss getroffen; killed = der Treffer zerstoert sie (dann ruft der Schuetze "Got him!" und
/// Kameraden in der Naehe "Man down!"). psProj darf nullptr sein (Dauerschaden wird ignoriert)
void onDroidDamaged(const DROID *psVictim, const PROJECTILE *psProj, bool killed);
/// die Einheit hat den Turm eines Gegners abgeschossen (jubelt: "Yeehaw!")
void onPartShot(const DROID *psShooter);
/// der lokale Spieler hat der Einheit einen Befehl gegeben (Maus, Taste, Bedienfeld); order ist DROID_ORDER
void onPlayerOrder(const DROID *psDroid, int order);
/// Einheit ruft um Hilfe ("Requesting support")
void onHelpCall(const DROID *psDroid);
/// MOD (16.11.): sofortiger Ruf einer Kategorie (z. B. "inf_incoming", "inf_spread", "tnk_spread", "inf_dig") - nur wenn die Einheit nah an der
/// Kamera und sichtbar ist, mit eigener kurzer Sperre gegen Ueberlagerung. Reine Tonausgabe, aendert nichts am Spiel.
bool shout(const DROID *psDroid, const char *cat);
/// Klasse der Einheit als Vorsilbe ("inf", "tnk", "trn", "pil")
const char *classPrefix(const DROID *psDroid);
/// jeden Spielschritt aus der Spielschleife (rechnet selbst nur alle 250 ms)
void tick(GameWorld &world);
/// bei neuem Spiel / Laden
void reset();
}
