// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Funkgebrabbel. In der Naehe von Kommandozentralen (HQ) und ATC-Tuermen (Feature "AirTrafficControl") hoert man ab und zu
// unverstaendlichen Flugsicherungs-Funk (audio/radio/atc_a..d.ogg). Reine Anzeige/Ton: beeinflusst den Spielzustand nicht,
// benutzt deshalb auch nicht gameRand. Spielt nur, wenn die Kamera nahe genug ist und der Ort fuer den lokalen Spieler sichtbar ist.
#pragma once

struct GameWorld;

namespace modradio
{
/// jeden Spielschritt aus der Spielschleife (rechnet selbst nur alle paar Sekunden)
void tick(GameWorld &world);
/// beim Spielende / Ladevorgang
void reset();
}
