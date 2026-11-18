// MOD (18.11.): Schildkuppel wie Supreme Commander 1 (UEF-Schildgenerator), Rechnung je Bildpunkt.
// Kuppel (Modus 0): vier Texturlagen laufen gegeneinander (Massstab 1, 3, 32, 6; Verschiebung kommt fertig aus dem Programm):
//   tex  = Kuppel-Grundbild: rg Wolken (Lage 2), b Bodensaum, a Ausblenden an den Polen (Lage 1, fest)
//   tex1 = feines Korn (Lage 3), tex2 = Rauschen in drei Kanaelen (Lage 4)
// Aus dem Vergleich der Kanaele ergeben sich wenige feste Deckkraft-Stufen - daher das typische "elektrische" Flimmern.
// Bei wenig Schildpunkten pulsiert die Kuppel rot. Einschlag (Modus 1): Ring laeuft nach aussen und verblasst (additiv).
// Gerechnet wird in Anzeigewerten (wie damals); bei linearer Beleuchtung wird das Ergebnis am Ende umgerechnet.

vec4 modShieldDome(vec4 mask, vec4 cloud, vec4 grain, vec4 noise)
{
	vec3 n = grain.gaa * 2.0 - 1.0;
	n.z = sqrt(abs(1.0 - n.x * n.x - n.y * n.y));     // wie auf der Grafikkarte von damals (Wurzel aus dem Betrag)
	float d = dot(cloud.rgr, n);
	vec4 c = vec4(d, d, d + 0.25, 1.0);
	float p = params.x;
	float a;
	if (noise.g <= cloud.r)
	{
		if (noise.b >= cloud.g) { a = (c.b >= n.z) ? 0.12 : mix(0.05, 0.0, p); }
		else { a = (n.z >= cloud.r) ? 0.2 : mix(0.01, 0.1, p); }
	}
	else
	{
		if (noise.r >= cloud.r) { a = (noise.b >= cloud.r) ? 0.025 : 0.1; }
		else { a = (noise.g >= cloud.g) ? 0.02 : mix(0.37, 0.46, p); }
	}
	c.a = a;
	c.rgb += vec3(0.0, 0.0, 0.15);
	// wenig Schildpunkte: rot pulsieren
	vec4 m1 = mix(vec4(0.5, 0.0, 0.0, 0.05), c, 0.5);
	m1 = mix(c, m1 + c, params.y);
	c = mix(m1, c, params.z);
	// Bodensaum
	c += vec4(mask.b * 0.95);
	// Pole ausblenden
	c.a *= mask.a;
	c = clamp(c, 0.0, 1.0);
	c.a *= params2.x;
	return c;
}

vec4 modShieldImpact(vec4 ring, vec4 detail, float edgeMask)
{
	vec3 c = clamp(vec3(0.0, 0.0, 0.5) + vec3(detail.r), 0.0, 1.0);
	float a = clamp(edgeMask * ring.g * (detail.r + detail.g) * params2.x, 0.0, 1.0);
	return vec4(c, a);
}

vec4 modShieldFinish(vec4 result, vec3 posView)
{
	int linearLighting = int(fogRange.w);
	vec3 rgb = result.rgb;
	if (linearLighting != 0)
	{
		rgb = wzSrgbToLinear(rgb);
	}
	if (fogRange.z > 0.5)
	{
		float fogAmount = wzDistanceFogAmount(length(posView), fogRange.x, fogRange.y);
		rgb = wzApplyForwardFog(rgb, result.a, fogAmount, fogColor.rgb, (params.w < 0.5) ? WZ_FOG_OUTPUT_STRAIGHT_ALPHA : WZ_FOG_OUTPUT_ADDITIVE, linearLighting);
	}
	return vec4(rgb, result.a);
}
