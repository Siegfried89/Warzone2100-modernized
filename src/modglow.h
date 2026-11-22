// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 15:49): "Siehst du den Unterschied zwischen den schoenen leuchtenden sauberen Kreisen links und den haesslichen Kreisen
// und Pfeilen rechts? Mit dem Zwischenraum?" - Die Schlachtplan-Striche bestanden aus vielen sich ueberlappenden weichen Kapseln (Perlen an
// den Ueberlappungen, sichtbare Stufen zwischen Kern, Hof und Lichthof). Hier: jedes Band ist EIN durchgehender Dreiecksstreifen mit
// fliessendem Querschnitt (weisser Kern - Farbe - weicher Lichthof), ohne Ueberlappung, also sauber wie die Reichweitenringe.
#pragma once

#include <glm/glm.hpp>

#include <utility>
#include <vector>

namespace modglow
{
struct Colour
{
	unsigned char r, g, b;
};

/// Leuchtendes Band entlang einer Bildschirmlinie. w = halbe Breite des farbigen Koerpers, glow = Breite des Lichthofs (Bildpunkte).
/// scale (leer oder je Punkt): Breitenfaktor (Pfeilschwanz), alphaMul (leer oder je Punkt): Deckkraftfaktor. roundCaps: offene Enden laufen weich aus.
void ribbon(const std::vector<glm::vec2> &pts, float w, float glow, Colour c, float alpha, bool closed,
            const std::vector<float> &scale = {}, const std::vector<float> &alphaMul = {}, bool roundCaps = true);

/// Pfeilspitze: gefuelltes Dreieck (Basis-Mitte base, Spitze tip, halbe Basisbreite halfW) mit hellem Kern und Lichthof an den Flanken
void arrowHead(const glm::vec2 &base, const glm::vec2 &tip, float halfW, float glow, Colour c, float alpha);

/// kurze Querstriche (Verteidigungslinie): alle in einem Aufruf, mit weicher Kante statt grossem Lichthof
void bars(const std::vector<std::pair<glm::vec2, glm::vec2>> &bars, float halfW, Colour c, float alpha);

/// GPU-Puffer freigeben (Spielende)
void shutdown();
}
