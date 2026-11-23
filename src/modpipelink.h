// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 19:57): "Ein Klick auf den Oilrig bringt den WZ-Stil-Button 'mit naechstem Kraftwerk verbinden'. Falls das am
// naechsten stehende Kraftwerk bereits voll waere, wuerde ein bestehender Oilrig zu einem passenderen wechseln, um nicht unnoetig lange
// Wege der Pipeline zu haben und Pipelines direkt am naechsten Kraftwerk/Basis vorbei zu haben."
// Ist genau ein eigener Oelturm gewaehlt, erscheint unten in der Mitte ein Knopf im WZ-Stil. Er sucht den kuerzesten Rohrweg vom Oelturm
// zum naechsten Rohrnetz mit freiem Kraftwerksplatz (oder direkt zum naechsten Kraftwerk mit Platz) und schickt die naechsten Bautrupps
// los, die Pipeline Stueck fuer Stueck zu legen. Die Zuteilung im Netz (wer an welches Kraftwerk geht) macht modoil jede Sekunde neu -
// dort wechselt ein weiter entfernter Oelturm zum naechstbesten Kraftwerk, wenn ein naeherer dazukommt.
#pragma once

namespace modpipelink
{
/// jedes Bild aus intRunWidgets
void tick();
void shutdown();
}
