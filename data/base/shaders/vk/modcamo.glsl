// MOD (18.11. 19:20 "Die Wuesten-Tarnfarbe der Soldaten (Hosen) bitte als neue Spielerfarbe einfuehren. Sodass dann ganze Panzerketten mit
// dem Wuestentarn cool aussehen koennen."): Spielerfarbe 16 "Wuestentarn" = digitales Wuestentarnmuster wie die Hosen der NP-Soldaten
// (Farben aus page-32-npsoldc.png: Sand, Hellbraun, Braun, vereinzelt helle Pixel). Die Engine meldet es mit Teamfarbe-Alpha < 0.02
// (iIMDShape::getTeamColourForModel). Das Muster haengt am Modell (Punkt vor der Modell-Matrix): es faehrt mit, flimmert nicht und hat auf
// allen Fahrzeugen und Gebaeuden dieselbe Pixelgroesse. In der Ferne geht es weich in den Mittelton ueber (kein Flimmern).

float modCamoHash(vec3 q)
{
	return fract(sin(dot(q, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

float modCamoNoise(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float x0 = mix(modCamoHash(i), modCamoHash(i + vec3(1.0, 0.0, 0.0)), f.x);
	float x1 = mix(modCamoHash(i + vec3(0.0, 1.0, 0.0)), modCamoHash(i + vec3(1.0, 1.0, 0.0)), f.x);
	float x2 = mix(modCamoHash(i + vec3(0.0, 0.0, 1.0)), modCamoHash(i + vec3(1.0, 0.0, 1.0)), f.x);
	float x3 = mix(modCamoHash(i + vec3(0.0, 1.0, 1.0)), modCamoHash(i + vec3(1.0, 1.0, 1.0)), f.x);
	return mix(mix(x0, x1, f.y), mix(x2, x3, f.y), f.z);
}

bool modCamoActive(vec4 teamColour)
{
	return teamColour.a < 0.02;
}

// p = Punkt im Modell (Modell-Einheiten)
vec4 modCamoColour(vec3 p)
{
	const float CELL = 1.8;                      // Kantenlaenge eines Tarn-Pixels
	const vec3 SAND = vec3(0.804, 0.733, 0.584);  // 205 187 149
	const vec3 TAN = vec3(0.671, 0.584, 0.443);   // 171 149 113
	const vec3 BROWN = vec3(0.502, 0.416, 0.302); // 128 106  77
	const vec3 LIGHT = vec3(0.882, 0.839, 0.722); // 225 214 184
	vec3 q = floor(p / CELL);
	float big = modCamoNoise(q / 4.5 + 17.0);    // grosse Flecken
	float mid = modCamoNoise(q / 1.9 + 3.0);     // ausgefranste Raender
	float px = modCamoHash(q);                   // einzelne Pixel
	float v = 0.60 * big + 0.30 * mid + 0.10 * px;
	vec3 c = SAND;
	if (v > 0.595)
	{
		c = BROWN;
	}
	else if (v > 0.50)
	{
		c = TAN;
	}
	else if (px > 0.93)
	{
		c = LIGHT;
	}
#ifndef MODCAMO_NO_DERIVATIVES
	// weit weg (ein Tarn-Pixel kleiner als ein Bildpunkt): weich in den Mittelton
	float w = length(fwidth(p)) / CELL;
	vec3 avg = 0.52 * SAND + 0.26 * TAN + 0.20 * BROWN + 0.02 * LIGHT;
	c = mix(c, avg, smoothstep(0.45, 1.1, w));
#endif
	return vec4(c, 1.0);
}
