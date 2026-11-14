function setupPowerModifier(player)
{
	if (powerType === 0)
	{
		setPowerModifier(85, player);
	}
	else if (powerType === 2)
	{
		setPowerModifier(125, player);
	}

	// MOD: Die Schwierigkeit der KI wirkt nicht mehr ueber das Einkommen, sondern wie in Metal Fatigue getrennt auf
	// Schaden, Kosten und Produktionstempo (Engine: modaidiff.cpp). Das Einkommen bleibt fuer alle gleich.
}

// Power modifier data is currently not saved. Reset it during eventGameLoaded for the moment.
function setupPowerModifierGameLoaded()
{
	for (let i = 0; i < maxPlayers; ++i)
	{
		setupPowerModifier(i);
	}
}
