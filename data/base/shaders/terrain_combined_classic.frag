// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

// Texture arrays are supported in:
// 1. core OpenGL 3.0+ (and earlier with EXT_texture_array)
//#if (!defined(GL_ES) && (__VERSION__ < 130))
//#extension GL_EXT_texture_array : require // only enable this on OpenGL < 3.0
//#endif
// 2. OpenGL ES 3.0+
#if (defined(GL_ES) && (__VERSION__ < 300))
#error Unsupported version of GLES
#endif

#include "terrain_combined.glsl"
#include "srgb.glsl"

// constants overridden by WZ when loading shaders (do not modify here in the shader source!)
#define WZ_SHADOW_MODE 1
#define WZ_SHADOW_FILTER_SIZE 3
#define WZ_SHADOW_CASCADES_COUNT 3
//


#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#define texture2DArray(tex,coord,bias) texture(tex,coord,bias)
#else
#define texture(tex,uv,bias) texture2D(tex,uv,bias)
#endif

uniform sampler2D lightmap_tex;

// ground texture arrays. layer = ground type
uniform sampler2DArray groundTex;
uniform sampler2DArray groundNormal;
uniform sampler2DArray groundSpecular;
uniform sampler2DArray groundHeight;

// array of scales for ground textures, encoded in mat4. scale_i = groundScale[i/4][i%4]

// decal texture arrays. layer = decal tile
uniform sampler2DArray decalTex;
uniform sampler2DArray decalNormal;
uniform sampler2DArray decalSpecular;
uniform sampler2DArray decalHeight;

// shadow map
uniform sampler2DArrayShadow shadowMap;


// sun light colors/intensity:


// fog

in vec2 uvLightmap;
in vec2 uvDecal;
in vec2 uvGround;
flat in int tile;
flat in uvec4 fgrounds;
flat in ivec4 fdiag;   // MOD (26.11.): Klassik - diagonale Nachbarkacheln (siehe modtApply)
in vec4 fgroundWeights;
// Light in tangent space:
in vec3 groundLightDir;
in vec3 groundHalfVec;
in mat2 decal2groundMat2;
// For Shadows
in vec3 posModelSpace;
in vec3 posViewSpace;

#ifdef NEWGL
out vec4 FragColor;
#else
// Uses gl_FragColor
#endif

#include "shadow_mapping.glsl"
#include "light.glsl"
#ifdef NEWGL

// MOD: Orbit wie in Metal Fatigue (17.11.): die Inseln sind die echten MF-Asteroiden-Modelle (Engine, display3d); unter der
// Leere-Kachel (256) liegt tief unten verschwommen die echte Planetenoberflaeche. Ihre Farbe je Feld (Oberflaechenkarte mit Relief)
// steht im Orbit in lightmap.rgb (src/terrain.cpp updateLightMap, modlayermap::orbitFloorColour) und wird hier am Bodenpunkt
// bilinear gelesen und weichgezeichnet - durchgehend, ohne Kachelraender.
const int MODV_TILE = 256;
const float MODV_DEPTH = 4600.0;         // so tief liegt der Boden unter den Inseln

vec3 modvLight()
{
	return clamp(ambientLight.rgb * 0.55 + diffuseLight.rgb * 0.55, vec3(0.3), vec3(1.2));
}

// Planetenboden am Punkt g (Modellkoordinaten), weichgezeichnet ueber etwa 'spread' Felder
// (17.11.) ausserhalb der Karte: gespiegelt fortgesetzt (keine Streifen vom Kartenrand), zunehmend weich; w = wie weit draussen (0..)
vec3 modvFloor(vec3 g, float spread, out float outside)
{
	vec2 uv = (ModelUVLightmapMatrix * vec4(g, 1.0)).xy;
	vec2 o = max(max(-uv, uv - 1.0), vec2(0.0));
	outside = max(o.x, o.y);
	uv = 1.0 - abs(1.0 - mod(uv, 2.0));
	spread *= 1.0 + outside * 60.0;
	vec2 px = spread / vec2(textureSize(lightmap_tex, 0));
	vec3 c = texture(lightmap_tex, uv).rgb * 0.36;
	c += texture(lightmap_tex, uv + vec2( 0.8,  0.3) * px).rgb * 0.16;
	c += texture(lightmap_tex, uv + vec2(-0.8, -0.3) * px).rgb * 0.16;
	c += texture(lightmap_tex, uv + vec2(-0.3,  0.8) * px).rgb * 0.16;
	c += texture(lightmap_tex, uv + vec2( 0.3, -0.8) * px).rgb * 0.16;
	if (linearLighting != 0) { c = wzSrgbToLinear(c); }
	return c;
}

vec3 modvColor(vec3 P)
{
	vec3 V = P - cameraPos.xyz;
	float lenV = max(length(V), 1.0);
	float down = max(-V.y, 0.02 * lenV);
	vec3 dir = V / down;                 // Weg je Einheit Abstieg
	vec3 lightC = modvLight();
	vec3 g = P + dir * MODV_DEPTH;
	float distG = lenV + length(dir) * MODV_DEPTH;
	float outside;
	vec3 col = modvFloor(g, 1.6, outside);
	vec3 haze = mix(modvFloor(g, 9.0, outside), vec3(0.70, 0.72, 0.76), 0.45) * lightC;
	// ausserhalb der Karte geht der Boden weich in den Dunst ueber
	return mix(col * lightC, haze, max(0.15 + 0.85 * smoothstep(9000.0, 32000.0, distG), smoothstep(0.0, 0.35, outside)));
}
#endif

#ifdef NEWGL
#define MODC_TEXGRAD(tex, coord, dx, dy) textureGrad(tex, coord, dx, dy)
#else
#define MODC_TEXGRAD(tex, coord, dx, dy) texture2DArray(tex, coord, 0.0)
#endif
// MOD (17.11., "Die Betontexturen laufen nirgends weich aus"): Klassik-Grafik - weicher Betonrand und Wasserflaeche.
// Die Engine (src/modconcrete.cpp classicDecal) kodiert in der Kachelnummer: 1048576 * k + 4096 * Randmaske + eigene Kachel (4095 = keine).
// k = 1: reines Wasserfeld -> Wasserkachel 17 weltfest gezeichnet (darunter scheint der Wasserdurchgang leicht durch).
// k >= 2: Feld neben Beton -> der Beton (Kachel (k-2)/2, Feinraster (k-2)%2) laeuft von den markierten Raendern/Ecken aus weich und
// unregelmaessig (Rauschen) etwa 0,4 Feld weit auf dieses Feld aus. Nur ganzzahlige Grundrechenarten (keine Bit-Operatoren).
const int MODC_BASE = 1048576;
int modcLayer(int t)
{
	if (t < MODC_BASE) { return t; }
	int r = t - (t / 4096) * 4096;
	return (r == 4095) ? -1 : r;
}
float modcHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float modcNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(modcHash(i), modcHash(i + vec2(1.0, 0.0)), f.x), mix(modcHash(i + vec2(0.0, 1.0)), modcHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
bool modcBit(int m, int b) { int q = m / b; return (q - (q / 2) * 2) == 1; }
#ifdef NEWGL
// ===== MOD (Wunsch 26.11.) gepixelte Uebergaenge =====
// "Fehlende Transitions, harte Schnitte bei den Tiles - fuer alles Uebergaenge, aber NICHT blurry, sondern schoen pixelig und stark,
// sodass es den Pixel-Look nicht verliert" und (17:31) "die kleinste Transition nicht nur aus zwei Pixeln, sondern auch aus einzelnen -
// fein auslaufen". Jeder Texel (Bildpunkt der Kacheltextur) entscheidet ganz oder gar nicht (kein Weichzeichnen) ueber eine Schwelle aus
// Zufallswerten je 8er-, 4er-, 2er- und 1er-Texel-Block: grobe Klumpen, deren Raender zu einzelnen Pixeln ausfransen.
uint modtHash(ivec2 p)
{
	uint h = uint(p.x) * 73856093u ^ uint(p.y) * 19349663u;
	h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u;
	return h;
}
float modtRand(ivec2 p, int salt)
{
	return float(modtHash(p + ivec2(salt * 7919, salt * 15401)) & 65535u) / 65535.0;
}
// Schwelle eines Welt-Texels tx (Mittel 0,5, symmetrisch): lvMin = feinste Stufe, die noch groesser als ein Bildschirmpixel ist
float modtThresh(ivec2 tx, int lvMin, int salt)
{
	float t = 0.10 * modtRand(tx >> max(0, lvMin), salt * 4);
	t += 0.15 * modtRand(tx >> max(1, lvMin), salt * 4 + 1);
	t += 0.25 * modtRand(tx >> max(2, lvMin), salt * 4 + 2);
	t += 0.50 * modtRand(tx >> max(3, lvMin), salt * 4 + 3);
	return t;
}
// feinste Stufe aus der Bildschirmgroesse eines Texels (Ableitungen der Kachel-UV, 1 = ein Feld)
int modtLevel(vec2 gx, vec2 gy, float texN)
{
	float texelsPerPixel = max(max(length(gx), length(gy)) * texN, 1e-4);
	return clamp(int(ceil(log2(texelsPerPixel) - 0.25)), 0, 7);
}
// Abstand (in Feldern) vom Punkt f (0..1 im Feld) zum Nachbarfeld k: 0 N, 1 O, 2 S, 3 W, 4 NO, 5 SO, 6 SW, 7 NW
float modtDist(vec2 f, int k)
{
	if (k == 0) { return f.y; }
	if (k == 1) { return 1.0 - f.x; }
	if (k == 2) { return 1.0 - f.y; }
	if (k == 3) { return f.x; }
	if (k == 4) { return length(vec2(1.0 - f.x, f.y)); }
	if (k == 5) { return length(vec2(1.0 - f.x, 1.0 - f.y)); }
	if (k == 6) { return length(vec2(f.x, 1.0 - f.y)); }
	return length(f);
}
// (26.11.) halbdurchsichtige Stellen einer Kachel (Ufer, Krater, Gleise, Decals) nicht weich, sondern gepixelt: je Texelblock ganz oder gar
// nicht, Wahrscheinlichkeit = Deckkraft in der Blockmitte
vec4 modtAlpha(vec4 decal, float layer, vec2 uv, vec2 gx, vec2 gy)
{
	float texN = float(textureSize(decalTex, 0).x);
	int lv = modtLevel(gx, gy, texN);
	float bn = texN / exp2(float(lv));
	vec2 bq = floor(uv * bn);
	float a = textureGrad(decalTex, vec3((bq + 0.5) / bn, layer), gx, gy).a;
	if (a <= 0.004) { return vec4(decal.rgb, 0.0); }
	if (a >= 0.996) { return vec4(decal.rgb, 1.0); }
	float T = modtThresh((ivec2(bq) << lv) + ivec2(int(layer) * 1031, 0), lv, 3);
	return vec4(decal.rgb, T < a ? 1.0 : 0.0);
}
#endif
vec4 modcSample(float layer, vec2 uv)
{
	// weltfeste UV (gekachelt), Ableitungen der ungebrochenen UV -> keine Mip-Naht an den Kachelgrenzen
	return MODC_TEXGRAD(decalTex, vec3(fract(uv), layer), dFdx(uv), dFdy(uv));
}
vec4 modcApply(vec4 decal, int t, vec3 P)
{
	if (t < MODC_BASE) { return decal; }
	int k = t / MODC_BASE;
	vec2 w = vec2(P.x, -P.z);
	if (k == 1)
	{
#ifdef NEWGL
		if (decal.a > 0.5) { return decal; }   // (26.11.) Uebergangs-Pixel der Nachbarfelder (modtApply) bleiben
#endif
		vec4 wc = modcSample(17.0, w / 512.0);
		return vec4(wc.rgb, 0.85);
	}
	int hi = k - 2;
	int lay = hi / 2;
	int fine = hi - lay * 2;
	int m = (t - k * MODC_BASE) / 4096;
	float size = (fine == 1) ? 64.0 : 128.0;
	vec2 f = fract(w / size);
	float d = 9.0;
	if (modcBit(m, 1)) { d = min(d, f.y); }
	if (modcBit(m, 2)) { d = min(d, 1.0 - f.x); }
	if (modcBit(m, 4)) { d = min(d, 1.0 - f.y); }
	if (modcBit(m, 8)) { d = min(d, f.x); }
	if (modcBit(m, 16)) { d = min(d, length(vec2(1.0 - f.x, f.y))); }
	if (modcBit(m, 32)) { d = min(d, length(vec2(1.0 - f.x, 1.0 - f.y))); }
	if (modcBit(m, 64)) { d = min(d, length(vec2(f.x, 1.0 - f.y))); }
	if (modcBit(m, 128)) { d = min(d, length(vec2(f.x, f.y))); }
	d *= size;   // Weltmass
	float n = ((modcNoise(w / 37.0) - 0.5) * 34.0 + (modcNoise(w / 11.0) - 0.5) * 14.0) * smoothstep(0.0, 14.0, d);
	// (18.11. 02:44 "genauso wie bei der LZ ... maximal die Haelfte (einer Kachel), wenn ueberhaupt"): Beton unter Gebaeuden laeuft genau so
	// weit aus wie der Karten-Beton der LZ (etwa 0,4 Feld)
	float reach = 54.0;
#ifdef NEWGL
	// (26.11.) gepixelt statt weich: je Texelblock (Mitte) ganz Beton oder gar nicht, Wahrscheinlichkeit = bisheriger weicher Anteil
	float texN = float(textureSize(decalTex, 0).x);
	vec2 wt = w / 128.0;                                          // Weltlage in Feldern
	int lv = modtLevel(dFdx(wt), dFdy(wt), texN);
	float bs = exp2(float(lv)) / texN;                            // Blockgroesse in Feldern
	vec2 wq = (floor(wt / bs) + 0.5) * bs;                        // Blockmitte
	vec2 fq = fract(wq * 128.0 / size);
	float dq = 9.0;
	if (modcBit(m, 1)) { dq = min(dq, fq.y); }
	if (modcBit(m, 2)) { dq = min(dq, 1.0 - fq.x); }
	if (modcBit(m, 4)) { dq = min(dq, 1.0 - fq.y); }
	if (modcBit(m, 8)) { dq = min(dq, fq.x); }
	if (modcBit(m, 16)) { dq = min(dq, length(vec2(1.0 - fq.x, fq.y))); }
	if (modcBit(m, 32)) { dq = min(dq, length(vec2(1.0 - fq.x, 1.0 - fq.y))); }
	if (modcBit(m, 64)) { dq = min(dq, length(vec2(fq.x, 1.0 - fq.y))); }
	if (modcBit(m, 128)) { dq = min(dq, length(vec2(fq.x, fq.y))); }
	dq *= size;
	vec2 wqw = wq * 128.0;
	float nq = ((modcNoise(wqw / 37.0) - 0.5) * 34.0 + (modcNoise(wqw / 11.0) - 0.5) * 14.0) * smoothstep(0.0, 14.0, dq);
	float a = 1.0 - smoothstep(0.0, reach, dq + nq * (reach / 54.0));
	if (a <= 0.001 || modtThresh(ivec2(floor(wt * texN)), lv, 2) >= a) { return decal; }
	vec4 c = modcSample(float(lay), w / 128.0);
	return vec4(c.rgb, 1.0);
#else
	float a = 1.0 - smoothstep(0.0, reach, d + n * (reach / 54.0));
	if (a <= 0.001) { return decal; }
	vec4 c = modcSample(float(lay), w / 128.0);
	return vec4(mix(decal.rgb, c.rgb, a), max(decal.a, a));
#endif
}


#ifdef NEWGL
// Klassik: Kachel-Uebergaenge. Daten je Feld (terrain.cpp getTileDecalInfo), Kachelnummern 9 Bit (511 kein Uebergang, 510 reines Wasserfeld):
//   grounds = untere 8 Bit der Nachbarn N, O, S, W; groundWeights-Bytes = Lage (Bit 0/1 Ursprung u/v gespiegelt, Bit 2 Achsen getauscht),
//     Bit 3 schmaler Saum, Bit 4 = Bit 8 der Kachelnummer, Bit 7 immer
//   diag (Tangente) = NO, SO, SW, NW: x = NO + 512 SO, y = SW + 512 NW, z = deren Lage/Saum je 4 Bit,
//     w = 1 + 2 * eigene Lage + 16 * Ufer + 32 * Feinraster (Kachel-Viertelung: ein Feld zeigt ein Viertel der Kacheltextur)
// Je Texel gilt der naechste abweichende Nachbar; an der Kante zeigt jede Seite die Haelfte der Texel vom Nachbarn (beide Seiten benutzen
// dieselbe Schwelle - die niedrigere Kachelnummer T, die hoehere 1 - T - so passen die Pixel beider Seiten genau ineinander), nach innen
// immer weniger. Der Nachbar wird an der Kante gespiegelt gelesen (mit seiner Drehung), Ecken punktgespiegelt.
// Saum: andere Bodenart 0,6 Kachel, gleiche Bodenart / Strasse / Klippe / Wasser 0,3 Kachel (Feinraster: hoechstens ein Feld).
vec4 modtApply(vec4 decal, int ownKey, vec2 gx, vec2 gy, vec2 wuv, vec2 wgx, vec2 wgy)
{
	uvec4 nbs = fgrounds;
	ivec4 dg = fdiag;
	if (dg.w < 1) { return decal; }
	ivec4 code = ivec4(floor(fgroundWeights * 255.0 + 0.5));
	int lay[8];
	int cd[8];
	lay[0] = int(nbs.x) | ((code.x & 16) << 4); lay[1] = int(nbs.y) | ((code.y & 16) << 4);
	lay[2] = int(nbs.z) | ((code.z & 16) << 4); lay[3] = int(nbs.w) | ((code.w & 16) << 4);
	cd[0] = code.x & 15; cd[1] = code.y & 15; cd[2] = code.z & 15; cd[3] = code.w & 15;
	lay[4] = dg.x & 511; lay[5] = (dg.x >> 9) & 511; lay[6] = dg.y & 511; lay[7] = (dg.y >> 9) & 511;
	cd[4] = dg.z & 15; cd[5] = (dg.z >> 4) & 15; cd[6] = (dg.z >> 8) & 15; cd[7] = (dg.z >> 12) & 15;
	bool any = false;
	for (int k = 0; k < 8; k++) { any = any || lay[k] != 511; }
	if (!any) { return decal; }
	int own = (dg.w >> 1) & 7;
	bool fineGrid = (dg.w & 32) != 0;
	float S = fineGrid ? 2.0 : 1.0;                          // Felder je Kacheltextur
	// Lage in der Kacheltextur genau aus der Kachel-UV (die Weltlage ist an den Feldkanten nicht genau genug), dann im Feld
	vec2 ab0 = vec2((own & 1) != 0 ? 1.0 - uvDecal.x : uvDecal.x, (own & 2) != 0 ? 1.0 - uvDecal.y : uvDecal.y);
	vec2 ft = clamp((own & 4) != 0 ? ab0.yx : ab0, 0.0, 1.0);
	vec2 par = fineGrid ? min(floor(ft * 2.0), vec2(1.0)) : vec2(0.0);   // welches Viertel (Feinraster)
	vec2 f = ft * S - par;
	vec2 tc = uvLightmap * vec2(textureSize(lightmap_tex, 0)) - 0.5;   // Weltlage in Feldern (ein Lichtkarten-Texel ist ein Feld)
	ivec2 tileO = ivec2(floor(tc - f + 0.5));
	float texN = float(textureSize(decalTex, 0).x) / S;      // Texel je Feld
	int lv = modtLevel(gx * S, gy * S, texN);
	// Entscheidung je Block der feinsten Stufe: Mitte des Blocks
	float bn = max(1.0, texN / exp2(float(lv)));             // Bloecke je Feld
	vec2 bq = min(floor(f * bn), vec2(bn - 1.0));
	vec2 fc = (bq + 0.5) / bn;
	ivec2 tx = tileO * int(texN) + ivec2(bq) * (1 << lv);     // Welt-Texel (Ecke des Blocks)
	float wide = fineGrid ? 1.0 : 0.6, narrow = fineGrid ? 0.6 : 0.3;
	float best = 9.0;
	int k = -1;
	for (int q = 0; q < 8; q++)
	{
		if (lay[q] == 511) { continue; }
		float dn = max(0.0, modtDist(fc, q) - 0.5 / bn) / (((cd[q] & 8) != 0) ? narrow : wide);   // Abstand der dem Nachbarn naechsten Blockseite
		if (dn < best) { best = dn; k = q; }
	}
	if (k < 0 || best >= 1.0) { return decal; }
	float wob = (modtRand(ivec2(floor((vec2(tileO) + fc) * 3.0 / S)), 3) - 0.5) * 0.22;   // Saum nicht ueberall gleich breit
	float p = 0.5 * (1.0 - best - wob);
	int c = cd[k];
	int layer = lay[k];
	bool lower = ownKey < layer || (ownKey == layer && own < (c & 7));
	float T = modtThresh(tx, lv, 1);
	if ((lower ? T : 1.0 - T) >= p) { return decal; }
	vec4 n;
	if (layer == 510)
	{
		n = textureGrad(decalTex, vec3(fract(wuv), 17.0), wgx, wgy);   // Wasser wie das reine Wasserfeld (modcApply)
		n.a = 1.0;
	}
	else
	{
		// Spiegelpunkt im Nachbarfeld (Kanten: an der Kante gespiegelt, Ecken: an der Ecke), dann sein Viertel und seine Drehung
		vec2 m = (k >= 4) ? vec2(1.0) - f : ((k == 0 || k == 2) ? vec2(f.x, 1.0 - f.y) : vec2(1.0 - f.x, f.y));
		const ivec2 DXY[8] = ivec2[8](ivec2(0, -1), ivec2(1, 0), ivec2(0, 1), ivec2(-1, 0), ivec2(1, -1), ivec2(1, 1), ivec2(-1, 1), ivec2(-1, -1));
		vec2 npar = fineGrid ? vec2((ivec2(par) + DXY[k] + ivec2(2)) & ivec2(1)) : vec2(0.0);
		vec2 mt = (npar + m) / S;
		vec2 ab = (c & 4) != 0 ? mt.yx : mt;
		vec2 uv = vec2((c & 1) != 0 ? 1.0 - ab.x : ab.x, (c & 2) != 0 ? 1.0 - ab.y : ab.y);
		float g = exp2(WZ_MIP_LOAD_BIAS);
		n = textureGrad(decalTex, vec3(uv, float(layer)), gx * g, gy * g);
	}
	if (n.a < 0.5) { return decal; }   // durchsichtige Stellen des Nachbarn (Ufer, Krater, Gleise) nicht einstreuen
	return vec4(n.rgb, 1.0);
}
// Ufer-Felder (Wasser-Art): ihre durchsichtigen Stellen bekommen dasselbe Wasser wie die reinen Wasserfelder (keine Kante zwischen beiden)
vec4 modtShore(vec4 decal, vec2 wuv, vec2 wgx, vec2 wgy)
{
	if ((fdiag.w & 16) == 0 || decal.a >= 0.999) { return decal; }
	vec4 wc = textureGrad(decalTex, vec3(fract(wuv), 17.0), wgx, wgy);
	return vec4(mix(wc.rgb, decal.rgb, decal.a), max(decal.a, 0.85));
}
#endif

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_classic() {
	int modcBaseTile = modcLayer(tile);
#ifdef NEWGL
	vec2 modtGx = dFdx(uvDecal), modtGy = dFdy(uvDecal);
	vec2 modtW = vec2(posModelSpace.x, -posModelSpace.z) / 512.0;   // Wasser weltfest (wie modcApply)
	vec2 modtWx = dFdx(modtW), modtWy = dFdy(modtW);
#endif
	vec4 decal = modcBaseTile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(modcBaseTile)), WZ_MIP_LOAD_BIAS) : vec4(0.f);
#ifdef NEWGL
	// MOD (26.11.): gepixelte Kachel-Uebergaenge; reines Wasserfeld (k = 1) zaehlt als Kachel 510 und zeigt zuerst nichts (Wasser folgt)
	bool modtWaterOnly = tile >= MODC_BASE && tile < 2 * MODC_BASE;
	if (modtWaterOnly) { decal = vec4(0.0); }
	else if (modcBaseTile >= 0) { decal = modtAlpha(decal, float(modcBaseTile), uvDecal, modtGx, modtGy); }
	if (modcBaseTile >= 0) { decal = modtApply(decal, modtWaterOnly ? 510 : modcBaseTile, modtGx, modtGy, modtW, modtWx, modtWy); }
	decal = modtShore(decal, modtW, modtWx, modtWy);
#endif
	decal = modcApply(decal, tile, posModelSpace);   // MOD (17.11.): Betonrand (seit 26.11. gepixelt) / Wasserflaeche

	vec3 L = normalize(groundLightDir);
	vec3 N = vec3(0.f,0.f,1.f);
	float diffuseFactor = lambertTerm(N, L); // diffuse lighting
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, diffuseFactor, 0.001f);

	vec4 lightmap_vec4 = texture(lightmap_tex, uvLightmap, 0.f);
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
	c *= 1.06;   // Klassik (16.11. 19:07): nur ein kleines bisschen strahlender - nicht uebertreiben
	float l = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(l), c, 1.10);
	c = mix(c, c * c * (3.0 - 2.0 * c), 0.06);
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
#ifdef NEWGL
	vec4 fragColor = (tile == MODV_TILE) ? vec4(modvColor(posModelSpace), 1.0) : modGrade(main_classic());   // MOD: Orbit-Leere
#else
	vec4 fragColor = modGrade(main_classic());
#endif

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
