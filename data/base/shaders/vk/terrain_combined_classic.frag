#version 450

#include "terrain_combined.glsl"
#include "srgb.glsl"

layout (constant_id = 1) const uint WZ_SHADOW_MODE = 1;
layout (constant_id = 2) const uint WZ_SHADOW_FILTER_SIZE = 5;
layout (constant_id = 3) const uint WZ_SHADOW_CASCADES_COUNT = 3;

layout(set = 2, binding = 0) uniform sampler2D lightmap_tex;

// ground texture arrays. layer = ground type
layout(set = 2, binding = 1) uniform sampler2DArray groundTex;
layout(set = 2, binding = 2) uniform sampler2DArray groundNormal;
layout(set = 2, binding = 3) uniform sampler2DArray groundSpecular;
layout(set = 2, binding = 4) uniform sampler2DArray groundHeight;

// decal texture arrays. layer = decal tile
layout(set = 2, binding = 5) uniform sampler2DArray decalTex;
layout(set = 2, binding = 6) uniform sampler2DArray decalNormal;
layout(set = 2, binding = 7) uniform sampler2DArray decalSpecular;
layout(set = 2, binding = 8) uniform sampler2DArray decalHeight;

// depth map
layout(set = 2, binding = 9) uniform sampler2DArrayShadow shadowMap;

layout(location = 0) in FragData frag;
layout(location = 10) flat in FragFlatData fragf;

layout(location = 0) out vec4 FragColor;

#include "shadow_mapping.glsl"
#include "light.glsl"

// MOD: Orbit wie in Metal Fatigue (Ueberarbeitung 16.11. 18:06): die Inseln sind schwebende Felsbrocken ueber einem Wolkenmeer.
//  - lightmap.a traegt im Orbit ein Abstandsfeld: 0 = Leere, Inselkacheln = Abstand zum Rand in Feldern (1..6, kodiert 1 + d*42),
//    gesetzt in src/terrain.cpp updateLightMap / modlayermap::islandLevel. Bilinear gelesen ergibt es eine weiche Umrisslinie
//    (Ecken schraeg abgeschnitten wie in MF) und den Felskoerper, der sich nach unten verjuengt.
//  - Unter der Leere: Strahl nach unten -> erst der Felskoerper der Inseln (Gesteinstextur Kachel 257, Licht aus der Sonnenrichtung),
//    sonst tief unten die Wolken (Kachel 256) mit den Inselschatten.
const int MODV_TILE = 256;
const float MODV_TILE_SIZE = 128.0;
const float MODV_THICK = 1150.0;    // Hoehe des Felskoerpers unter der Inseloberflaeche
const float MODV_TAPER = 3.4;       // so viele Felder schrumpft der Koerper bis ganz unten
const float MODV_DEPTH = 4600.0;    // so tief liegen die Wolken unter den Inseln

float modvDistAt(ivec2 t, ivec2 sz)
{
	if (t.x < 0 || t.y < 0 || t.x >= sz.x || t.y >= sz.y) { return 0.0; }
	float a = texelFetch(lightmap_tex, t, 0).a * 255.0;
	return (a < 0.5) ? 0.0 : (a - 1.0) / 42.0;
}

// weiches Abstandsfeld (in Feldern) an einer Stelle (Modellkoordinaten x/z)
float modvField(vec2 xz)
{
	ivec2 sz = textureSize(lightmap_tex, 0);
	vec2 tc = vec2(xz.x, -xz.y) / MODV_TILE_SIZE - 0.5;
	ivec2 t0 = ivec2(floor(tc));
	vec2 fr = tc - vec2(t0);
	float v00 = modvDistAt(t0, sz);
	float v10 = modvDistAt(t0 + ivec2(1, 0), sz);
	float v01 = modvDistAt(t0 + ivec2(0, 1), sz);
	float v11 = modvDistAt(t0 + ivec2(1, 1), sz);
	return mix(mix(v00, v10, fr.x), mix(v01, v11, fr.x), fr.y);
}

bool modvIsland(vec2 xz)
{
	return modvField(xz) > 0.5;
}

float modvHash(vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float modvNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = modvHash(i), b = modvHash(i + vec2(1.0, 0.0)), c = modvHash(i + vec2(0.0, 1.0)), d = modvHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Schwelle des Felskoerpers in der Tiefe d (oben 0.5 = Umrisslinie, nach unten steigend = schmaler), unregelmaessig wie Fels
float modvThreshold(vec2 xz, float d)
{
	float k = clamp(d / MODV_THICK, 0.0, 1.0);
	float rough = (modvNoise(xz / 95.0 + vec2(d / 70.0, 0.0)) - 0.5) * 0.55 + (modvNoise(xz / 31.0 - vec2(0.0, d / 33.0)) - 0.5) * 0.22;
	return 0.5 + MODV_TAPER * pow(k, 0.85) + rough * smoothstep(0.0, 90.0, d);
}

vec3 modvLight()
{
	return clamp(ambientLight.rgb * 0.55 + diffuseLight.rgb * 0.55, vec3(0.3), vec3(1.2));
}

vec3 modvColor(vec3 P)
{
	vec3 V = P - cameraPos.xyz;
	float down = max(-V.y, 0.05 * length(V));
	vec3 dir = V / down;   // Weg je Einheit Abstieg
	vec3 lightC = modvLight();
	vec3 sd = normalize(sunPos.xyz);
	// 1. Felskoerper einer Insel (Strahl nach unten, dann eine Verfeinerung)
	const int STEPS = 30;
	float prevD = 0.0;
	for (int i = 1; i <= STEPS; ++i)
	{
		float d = MODV_THICK * float(i) / float(STEPS);
		vec3 q = P + dir * d;
		if (modvField(q.xz) > modvThreshold(q.xz, d))
		{
			float lo = prevD, hi = d;
			for (int r = 0; r < 4; ++r)
			{
				float m = 0.5 * (lo + hi);
				vec3 qm = P + dir * m;
				if (modvField(qm.xz) > modvThreshold(qm.xz, m)) { hi = m; } else { lo = m; }
			}
			d = hi;
			q = P + dir * d;
			// Flaechennormale aus dem Feldgefaelle (aussen + unten)
			float e = 24.0;
			float fx = (modvField(q.xz + vec2(e, 0.0)) - modvField(q.xz - vec2(e, 0.0))) / (2.0 * e);
			float fz = (modvField(q.xz + vec2(0.0, e)) - modvField(q.xz - vec2(0.0, e))) / (2.0 * e);
			vec3 n = normalize(vec3(-fx, -MODV_TAPER / MODV_THICK * 0.9, -fz));
			float lam = max(dot(n, sd), 0.0);
			vec2 uv = vec2((q.x - q.z) / 220.0, d / 220.0);
			vec3 rock = textureLod(decalTex, vec3(fract(uv), float(MODV_TILE + 1)), 1.0).rgb;
			float depthShade = mix(1.0, 0.45, smoothstep(0.0, MODV_THICK, d));
			vec3 col = rock * (0.38 + 0.85 * lam) * depthShade;
			// obere Kante etwas heller (Sonnenlicht auf dem Grat)
			col *= 1.0 + 0.25 * (1.0 - smoothstep(0.0, 70.0, d));
			return col * lightC;
		}
		prevD = d;
	}
	// 2. Wolkenmeer tief unten, mit den Schatten der Inseln
	vec3 g = P + dir * MODV_DEPTH;
	vec2 uvg = vec2(g.x, -g.z) / (MODV_TILE_SIZE * 4.0);
	vec3 col = textureLod(decalTex, vec3(fract(uvg), float(MODV_TILE)), 1.5).rgb;
	if (sd.y > 0.15)
	{
		float sh = 0.0;
		for (int s = 0; s < 4; ++s)
		{
			float up = MODV_DEPTH - MODV_THICK * (0.1 + 0.25 * float(s));
			vec3 r = g + sd * (up / sd.y);
			if (modvField(r.xz) > 0.5 + MODV_TAPER * (0.1 + 0.25 * float(s))) { sh += 0.25; }
		}
		col *= 1.0 - 0.35 * sh;
	}
	return mix(col, vec3(0.62, 0.66, 0.72), 0.22) * lightC;
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_classic() {
	vec4 decal = fragf.tileNo >= 0 ? texture(decalTex, vec3(frag.uvDecal, fragf.tileNo), WZ_MIP_LOAD_BIAS) : vec4(0);

	vec3 L = normalize(frag.groundLightDir);
	vec3 N = vec3(0,0,1);
	float diffuseFactor = lambertTerm(N, L); // diffuse lighting
	float visibility = getShadowVisibility(frag.posModelSpace, frag.posViewSpace, diffuseFactor, 0.001f);

	vec4 lightmap_vec4 = texture(lightmap_tex, frag.uvLightmap);

	if (fragf.tileNo > MODV_TILE) { lightmap_vec4.a = 1.0; }   // MOD: Orbit-Inseln - lightmap.a ist dort das Abstandsfeld, Helligkeit voll
	float tileBrightness = lightmap_vec4.a; // tile brightness / ambient occlusion
	if (linearLighting != 0)
	{
		// The tier's lighting was tuned for display values, so it is evaluated in display space
		vec3 lightD = (visibility * wzLinearToSrgb(diffuseLight.rgb) * diffuseFactor * 0.75 + wzLinearToSrgb(ambientLight.rgb) * 0.25) * tileBrightness;
		lightD = blendAddEffectLighting(lightD, lightmap_vec4.rgb / 1.5f);
		return vec4(wzSrgbToLinear(lightD * wzLinearToSrgb(decal.rgb)), decal.a);
	}
	vec3 diffuseTerm = (visibility*diffuseLight*diffuseFactor).rgb * 0.75;
	vec3 ambientTerm = ambientLight.rgb * 0.25;
	vec4 light = vec4(diffuseTerm + ambientTerm, ambientLight.a) * tileBrightness;
	light.rgb = blendAddEffectLighting(light.rgb, lightmap_vec4.rgb / 1.5f); // additive color (from environmental point lights / effects)
	light.a = 1.f;

	return light * decal;
}

// MOD (Wunsch 16.11.): Terrain strahlender - etwas heller, kraeftigere Farben, leicht mehr Kontrast (im Anzeige-Farbraum)
vec3 modGradeDisplay(vec3 c)
{
	c *= 1.24;   // Klassik (16.11. 18:30): noch strahlender als die anderen Stufen
	float l = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(l), c, 1.32);
	c = mix(c, c * c * (3.0 - 2.0 * c), 0.18);
	return clamp(c, 0.0, 1.0);
}
vec4 modGrade(vec4 c)
{
	if (linearLighting != 0)
	{
		return vec4(wzSrgbToLinear(modGradeDisplay(wzLinearToSrgb(c.rgb))), c.a);
	}
	return vec4(modGradeDisplay(c.rgb), c.a);
}

void main()
{
	vec4 fragColor = (fragf.tileNo == MODV_TILE) ? vec4(modvColor(frag.posModelSpace), 1.0) : modGrade(main_classic());   // MOD: Orbit-Leere
		// MOD (16.11. 18:06): Inselkacheln wie in MF - weiche Umrisslinie (schraege Ecken) mit hellem Rand; ausserhalb die Leere
	// (Felskoerper/Wolken), ebenso die schwarzen Stellen der MF-Kacheln (dort war in MF der Himmel)
	if (fragf.tileNo > MODV_TILE)
	{
		float modF = modvField(frag.posModelSpace.xz);
		if (modF < 0.6)
		{
			float modRim = 1.0 - smoothstep(0.0, 0.05, abs(modF - 0.54));
			fragColor.rgb = mix(fragColor.rgb, vec3(0.86, 0.83, 0.72) * modvLight(), 0.5 * modRim);
		}
		vec3 modIc = texture(decalTex, vec3(frag.uvDecal, fragf.tileNo), WZ_MIP_LOAD_BIAS).rgb;
		if (linearLighting != 0) { modIc = wzLinearToSrgb(modIc); }
		float modK = 1.0 - smoothstep(0.035, 0.11, max(modIc.r, max(modIc.g, modIc.b)));
		modK = max(modK, 1.0 - smoothstep(0.48, 0.52, modF));
		if (modK > 0.0) { fragColor = mix(fragColor, vec4(modvColor(frag.posModelSpace), 1.0), modK); }
	}

	FragColor = fragColor;
}
