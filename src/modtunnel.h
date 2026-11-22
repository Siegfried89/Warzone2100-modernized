// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 01:11): Tunnel wie die Nydus-Kanaele aus StarCraft - "Bohrer kann einen Tunnel von der Oberflaeche bis zu einem
// beliebigen Austrittspunkt graben, der Einheiten fuer Ueberraschungsangriffe dient. Diese Ein- und Ausgaenge koennen aber leicht
// zerstoert werden und alle darin befindlichen Einheiten [sterben] auf einen [Schlag]."
//
// Ablauf:
//  - Ein Bohrfahrzeug (Konstruktionsteil "DrillRig") bekommt den Bauauftrag "Tunnel Mouth" (A0TunnelMouth) an der Stelle des Ausgangs
//    (Baumenue, oder im Schlachtplan "Dig Tunnel": Linie vom Eingang zum Ausgang ziehen). Am Standort des Bohrers entsteht sofort der
//    Eingang, der Bohrer taucht in den Untergrund ab und bohrt sich geradeaus zum Ziel (der Gang ist im Untergrund zu sehen, oben steigt
//    fuer den eigenen Spieler Staub auf). Am Ziel bricht er durch, dort entsteht der Ausgang. Kosten: Baukosten des Tunnelmunds.
//  - Benutzen: Einheiten bekommen einen Bewegungsbefehl auf einen Tunnelmund (Rechtsklick auf den Mund). Am Mund verschwinden sie im
//    Untergrund, rasen durch den Gang und kommen am anderen Ende heraus; Befehle, die sie unterwegs bekommen (auch Shift-Befehle),
//    fuehren sie nach dem Austritt aus. Beide Richtungen.
//  - Wird ein Mund zerstoert, stuerzt der Tunnel ein: alle Einheiten darin sind verloren, der andere Mund faellt mit in sich zusammen.
// Alles laeuft im Spielschritt aus dem Spielzustand (auf allen Rechnern gleich).
#pragma once

#include <cstdint>
#include <vector>
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct DROID;
struct DroidOrder;
struct GameWorld;
struct BASE_OBJECT;
struct STRUCTURE_STATS;

namespace modtunnel
{
/// ist das der Tunnelmund?
bool isMouthStat(const STRUCTURE_STATS *psStats);
/// ist das Objekt ein fertiger Tunnelmund eines aktiven Tunnels, den dieser Spieler benutzen darf?
bool usableMouth(const BASE_OBJECT *psObj, unsigned player);
/// kann diese Einheit durch Tunnel fahren? (Bodeneinheiten, keine Transporter/Flieger)
bool canUse(const DROID *psDroid);
/// aus orderDroidBase (vor allem anderen): true, wenn der Befehl hier uebernommen wurde (Bohrer graebt / Einheit im Tunnel / Tunnel-Bauauftrag)
bool onOrder(DROID *psDroid, const DroidOrder *psOrder);
/// ist die Einheit gerade im Tunnel unterwegs?
bool inTransit(const DROID *psDroid);
/// graebt das Fahrzeug gerade einen Tunnel?
bool digging(const DROID *psDroid);
/// jeden Spielschritt
void tick(GameWorld &world);
/// Anzeige: eigene Tunnel (Erdband zwischen den Muendern) und laufende Grabungen
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);

/// fertiger Tunnel (fuer den Angriffsplaner)
struct Info
{
	uint32_t id = 0;
	Vector2i a{0, 0}, b{0, 0};   // Lage der Muender (Welt)
};
/// fertige Tunnel, die der Spieler benutzen darf
std::vector<Info> tunnels(unsigned player);

/// Spielstand
void saveState(std::vector<int64_t> &out);
void loadState(const std::vector<int64_t> &in);
/// Spielende / Laden
void reset();
}
