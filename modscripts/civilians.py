"""Zivilisten und Busse fuer die Stadtkarte: laeuft nur, wenn die Karte 'Stadt' geladen ist (mapName enthaelt 'Stadt').
Ein freier Spielerplatz (9) bekommt weisse Zivilisten (BaBa-Koerper, ohne Waffe) und kleine Busse/Eiswagen; sie sind mit allen echten Spielern
verbuendet (werden nicht beschossen) und laufen/fahren zufaellig ueber Strassen und Beton.
civilians.py OUT   -> haengt Funktionen an OUT/multiplay/script/rules/events/gameinit.js an und ruft sie am Ende von eventGameInit auf."""
import sys, os

OUT = sys.argv[1]
p = os.path.join(OUT, "multiplay", "script", "rules", "events", "gameinit.js")
s = open(p, encoding="utf-8").read()
if "cityLifeInit" in s:
    print("Zivilisten: schon vorhanden"); sys.exit(0)

ANCHOR = '\tsetTimer("autoSave", 10*60*1000);'
assert s.count(ANCHOR) == 1, "Anker fuer autoSave nicht eindeutig"
s = s.replace(ANCHOR, ANCHOR + "\n\ttry { cityLifeInit(); } catch (e) {}")

JS = r'''

// ---- MOD: Zivilisten und Busse fuer die Stadtkarte -------------------------------------------------
const CITY_CIV = 9;            // freier Spielerplatz fuer die Zivilisten
const CITY_PEOPLE = 64;
const CITY_VEHICLES = 10;
function cityLifeFreeSpot(x, y, range)
{
	try
	{
		const t = terrainType(x, y);
		if (t === TER_WATER || t === TER_CLIFFFACE) { return false; }
		return (enumRange(x, y, range, ALL_PLAYERS, false).length === 0);
	}
	catch (e) { return false; }
}
function cityLifeInit()
{
	var isCity = false;
	try { isCity = (String(mapName).indexOf("Stadt") >= 0); } catch (e) {}
	if (!isCity || maxPlayers > CITY_CIV) { return; }
	try { changePlayerColour(CITY_CIV, 10); } catch (e) {}
	for (let p = 0; p < maxPlayers; ++p)
	{
		try { setAlliance(p, CITY_CIV, true); } catch (e) {}
	}
	queue("cityLifeSpawn", 2000);
}
function cityLifeSpawn()
{
	const vehicles = [["BusBody", "Bus"], ["ScavIcevanBody", "Ice cream van"], ["BusBody", "Bus"]];
	var people = 0, cars = 0;
	for (let tries = 0; tries < 2500 && (people < CITY_PEOPLE || cars < CITY_VEHICLES); ++tries)
	{
		const x = 30 + syncRandom(196), y = 30 + syncRandom(196);
		if (!cityLifeFreeSpot(x, y, 1.5)) { continue; }
		if (people < CITY_PEOPLE && (cars >= CITY_VEHICLES || syncRandom(100) < 85))
		{
			if (addDroid(CITY_CIV, x, y, "Civilian", "B1BaBaPerson01", "BaBaLegs", "", "")) { ++people; }
		}
		else if (cars < CITY_VEHICLES)
		{
			const v = vehicles[syncRandom(vehicles.length)];
			if (addDroid(CITY_CIV, x, y, v[1], v[0], "BaBaProp", "", "")) { ++cars; }
		}
	}
	setTimer("cityLifeWander", 5000);
}
function cityLifeWander()
{
	const list = enumDroid(CITY_CIV);
	for (let i = 0; i < list.length; ++i)
	{
		const d = list[i];
		const car = (d.body === "BusBody" || d.body === "ScavIcevanBody");
		if (d.order !== DORDER_NONE && syncRandom(100) >= 12) { continue; }
		const r = car ? 10 + syncRandom(26) : 3 + syncRandom(11);
		const a = syncRandom(360) * Math.PI / 180;
		const tx = Math.floor(d.x + r * Math.cos(a)), ty = Math.floor(d.y + r * Math.sin(a));
		if (tx < 8 || ty < 8 || tx > mapWidth - 8 || ty > mapHeight - 8) { continue; }
		if (!cityLifeFreeSpot(tx, ty, 1)) { continue; }
		if (propulsionCanReach(d.propulsion, d.x, d.y, tx, ty)) { orderDroidLoc(d, DORDER_MOVE, tx, ty); }
	}
}
'''
s = s.rstrip("\n") + "\n" + JS
open(p, "w", encoding="utf-8").write(s)
print("Zivilisten: eingetragen (%d Zeilen)" % s.count("\n"))
