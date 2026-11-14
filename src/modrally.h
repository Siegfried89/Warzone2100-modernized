// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Sammelpunkt per Rechtsklick. Ist eine Fabrik (oder Reparaturanlage) angewaehlt und kein Fahrzeug, setzt der Befehlsklick auf freien
// Boden (Rechtsklick) ihren Sammelpunkt dorthin - wie in Metal Fatigue. Der Knopf "Lieferpunkt" in der Fabrikleiste entfaellt.
#pragma once

namespace modrally
{
/// true, wenn mindestens ein angewaehltes Fabrik-/Reparaturgebaeude einen neuen Sammelpunkt bekommen hat (dann ist der Klick verbraucht)
bool orderSelected(unsigned player, int worldX, int worldY);
}
