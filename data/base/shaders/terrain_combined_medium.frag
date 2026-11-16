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

// MOD: Orbit wie in Metal Fatigue (Ueberarbeitung 16.11. 21:00 nach den MF-Bildern des Nutzers): flache Inseln auf einem dicken,
// unten rund eingezogenen Felssockel; tief darunter verschwommen der Planetenboden (Kachel 256, je Tileset: Wueste/Stadt/Gebirge).
//  - lightmap.a traegt im Orbit ein Abstandsfeld: 0 = Leere, Inselkacheln = Abstand zum Rand in Feldern (1..6, kodiert 1 + d*42),
//    gesetzt in src/terrain.cpp updateLightMap / modlayermap::islandLevel. Bilinear gelesen ergibt es eine weiche Umrisslinie.
//  - Unter der Leere: Strahl nach unten -> erst der Felssockel einer Insel (Gestein Kachel 257, Licht aus der Sonnenrichtung),
//    sonst tief unten der Boden mit den Inselschatten. Die Schrittweite richtet sich nach dem Blickwinkel (keine Zacken/Treppen bei
//    flacher Kamera), Ferne geht in Dunst ueber, Texturen mit passender Mip-Stufe (kein Flimmern beim Drehen/Neigen der Kamera).
const int MODV_TILE = 256;
const float MODV_THICK = 760.0;          // Dicke des Felssockels unter der Inseloberflaeche
const float MODV_TAPER = 5.8;            // so viele Felder zieht der Sockel bis ganz unten ein (unten geschlossen)
const float MODV_DEPTH = 4600.0;         // so tief liegt der Boden unter den Inseln
const float MODV_REACH = 7000.0;         // so weit (waagrecht) wird nach Inselsockeln gesucht
const float MODV_FLOOR_REPEAT = 1024.0;  // Wiederholung der Bodentextur (Welteinheiten)
const float MODV_ROCK_REPEAT = 300.0;    // Wiederholung der Gesteinstextur (Welteinheiten)
const int MODV_MAX_STEPS = 72;

// Feldgroesse in Welteinheiten aus der Lichtkarten-Matrix (1 / (Feldgroesse * Lichtkartenbreite)) - gilt fuer jedes Raster (128 oder 64)
float modvTileSize()
{
	return 1.0 / max(abs(ModelUVLightmapMatrix[0][0]) * float(textureSize(lightmap_tex, 0).x), 0.000001);
}

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
	vec2 tc = vec2(xz.x, -xz.y) / modvTileSize() - 0.5;
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

// Schwelle des Sockels in der Tiefe d (oben 0.5 = Umrisslinie): oben fast senkrecht, unten rund eingezogen (wie in MF), unregelmaessig
float modvThreshold(vec2 xz, float d)
{
	float k = clamp(d / MODV_THICK, 0.0, 1.0);
	float rough = (modvNoise(xz / 110.0 + vec2(d / 90.0, 0.0)) - 0.5) * 0.5 + (modvNoise(xz / 37.0 - vec2(0.0, d / 41.0)) - 0.5) * 0.2;
	return 0.5 + 0.45 * k + MODV_TAPER * k * k * k + rough * smoothstep(0.0, 50.0, d);
}

vec3 modvLight()
{
	return clamp(ambientLight.rgb * 0.55 + diffuseLight.rgb * 0.55, vec3(0.3), vec3(1.2));
}

// Mip-Stufe fuer eine Textur, die sich alle 'repeat' Einheiten wiederholt, in Entfernung 'dist' unter dem Blickwinkel cosI
float modvLod(float dist, float cosI, float repeatUnits)
{
	float pix = 1.2 / float(max(viewportHeight, 1));                  // Blickwinkel je Bildpunkt (Bogenmass, ungefaehr)
	float foot = dist * pix / sqrt(max(cosI, 0.02));                   // Flaeche eines Bildpunkts auf der Oberflaeche (Einheiten)
	float texels = foot * float(textureSize(decalTex, 0).x) / repeatUnits;
	return max(log2(max(texels, 0.0001)), 0.0);
}

// Dunst in der Ferne: Mittelwert des Bodens (kleinste Mip-Stufe) mit etwas Himmelsgrau
vec3 modvHaze(vec3 lightC)
{
	vec3 avg = textureLod(decalTex, vec3(0.5, 0.5, float(MODV_TILE)), 14.0).rgb;
	return mix(avg, vec3(0.70, 0.72, 0.76), 0.45) * lightC;
}

vec3 modvRockTex(vec3 q, vec3 n, float lod)
{
	vec3 w = abs(n);
	w = w * w * w * w;
	w /= max(w.x + w.y + w.z, 0.0001);
	vec3 a = textureLod(decalTex, vec3(fract(vec2(q.z, q.y) / MODV_ROCK_REPEAT), float(MODV_TILE + 1)), lod).rgb;
	vec3 b = textureLod(decalTex, vec3(fract(vec2(q.x, q.z) / MODV_ROCK_REPEAT), float(MODV_TILE + 1)), lod).rgb;
	vec3 c = textureLod(decalTex, vec3(fract(vec2(q.x, q.y) / MODV_ROCK_REPEAT), float(MODV_TILE + 1)), lod).rgb;
	return a * w.x + b * w.y + c * w.z;
}

vec3 modvColor(vec3 P)
{
	vec3 V = P - cameraPos.xyz;
	float lenV = max(length(V), 1.0);
	float down = max(-V.y, 0.02 * lenV);
	vec3 dir = V / down;                 // Weg je Einheit Abstieg
	float hor = length(dir.xz);          // waagrechter Weg je Einheit Abstieg
	float cosI = down / lenV;            // Steilheit des Blicks (1 = senkrecht)
	vec3 lightC = modvLight();
	vec3 haze = modvHaze(lightC);
	vec3 sd = normalize(sunPos.xyz);
	// 1. Felssockel einer Insel: Strahl nach unten, waagrecht hoechstens ~40 Einheiten je Schritt, dann Feinsuche
	float dMax = min(MODV_THICK, MODV_REACH / max(hor, 0.001));
	int n = int(clamp(ceil(dMax * max(hor, 0.3) / 40.0), 10.0, float(MODV_MAX_STEPS)));
	float stepD = dMax / float(n);
	float prevD = 0.0;
	for (int i = 1; i <= MODV_MAX_STEPS; ++i)
	{
		if (i > n) { break; }
		float d = stepD * float(i);
		vec3 q = P + dir * d;
		if (modvField(q.xz) > modvThreshold(q.xz, d))
		{
			float lo = prevD, hi = d;
			for (int r = 0; r < 5; ++r)
			{
				float m = 0.5 * (lo + hi);
				vec3 qm = P + dir * m;
				if (modvField(qm.xz) > modvThreshold(qm.xz, m)) { hi = m; } else { lo = m; }
			}
			d = hi;
			q = P + dir * d;
			// Flaechennormale: Feldgefaelle waagrecht, Einzug der Schwelle nach unten
			float e = 24.0;
			float fx = (modvField(q.xz + vec2(e, 0.0)) - modvField(q.xz - vec2(e, 0.0))) / (2.0 * e);
			float fz = (modvField(q.xz + vec2(0.0, e)) - modvField(q.xz - vec2(0.0, e))) / (2.0 * e);
			float k = clamp(d / MODV_THICK, 0.0, 1.0);
			float dThr = (0.45 + 3.0 * MODV_TAPER * k * k) / MODV_THICK;
			vec3 nrm = normalize(vec3(-fx, -dThr, -fz));
			float lam = max(dot(nrm, sd), 0.0);
			float dist = length(q - cameraPos.xyz);
			vec3 rock = modvRockTex(q, nrm, modvLod(dist, max(abs(dot(nrm, -normalize(V))), 0.05), MODV_ROCK_REPEAT));
			vec3 col = rock * (0.40 + 0.85 * lam) * mix(1.0, 0.55, k);
			col *= 1.0 + 0.22 * (1.0 - smoothstep(0.0, 45.0, d));   // obere Kante heller (Sonnenlicht auf dem Grat)
			return mix(col * lightC, haze, smoothstep(4500.0, 18000.0, dist));
		}
		prevD = d;
	}
	// 2. Planetenboden tief unten (verschwommen wie in MF), mit den Schatten der Inseln
	vec3 g = P + dir * MODV_DEPTH;
	float distG = lenV + length(dir) * MODV_DEPTH;
	vec2 uvg = vec2(g.x, -g.z) / MODV_FLOOR_REPEAT;
	vec3 col = textureLod(decalTex, vec3(fract(uvg), float(MODV_TILE)), modvLod(distG, cosI, MODV_FLOOR_REPEAT) + 1.5).rgb;
	if (sd.y > 0.15)
	{
		float sh = 0.0;
		for (int s = 0; s < 4; ++s)
		{
			float kk = 0.1 + 0.25 * float(s);
			float up = MODV_DEPTH - MODV_THICK * kk;
			vec3 r = g + sd * (up / sd.y);
			if (modvField(r.xz) > 0.5 + 0.45 * kk + MODV_TAPER * kk * kk * kk) { sh += 0.25; }
		}
		col *= 1.0 - 0.35 * sh;
	}
	return mix(col * lightC, haze, 0.15 + 0.85 * smoothstep(9000.0, 32000.0, distG));
}
#endif

vec3 getGroundUv(int i) {
	uint groundNo = fgrounds[i];
	return vec3(uvGround * groundScale[groundNo/4u][groundNo%4u], float(groundNo));
}

vec3 getGround(int i) {
	vec3 color = texture2DArray(groundTex, getGroundUv(i), WZ_MIP_LOAD_BIAS).rgb;
	if (linearLighting != 0)
	{
		// The layer blend was tuned for display values, so re-encode each layer before the weighted sum
		color = wzLinearToSrgb(color);
	}
	return color * fgroundWeights[i];
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_medium() {
	vec3 ground = getGround(0) + getGround(1) + getGround(2) + getGround(3);
	vec4 decal = tile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(tile)), WZ_MIP_LOAD_BIAS) : vec4(0.f);
	if (linearLighting != 0)
	{
		// The decal mixes over the layers in display space as well
		decal.rgb = wzLinearToSrgb(decal.rgb);
	}

	vec3 L = normalize(groundLightDir);
	vec3 N = vec3(0.f,0.f,1.f);
	float diffuseFactor = lambertTerm(N, L); // diffuse lighting

	float visibility = getShadowVisibility(posModelSpace, posViewSpace, diffuseFactor, 0.001f);

	vec4 lightmap_vec4 = texture(lightmap_tex, uvLightmap, 0.f);
#ifdef NEWGL

	if (tile > MODV_TILE) { lightmap_vec4.a = 1.0; }   // MOD: Orbit-Inseln - lightmap.a ist dort das Abstandsfeld, Helligkeit voll
#endif
	float tileBrightness = lightmap_vec4.a; // tile brightness / ambient occlusion
	vec3 albedo = (1.f - decal.a) * ground + decal.a * decal.rgb;
	if (linearLighting != 0)
	{
		// The tier's lighting was tuned for display values, so it is evaluated in display space
		vec3 diffuseD = wzLinearToSrgb(diffuseLight.rgb);
		vec3 ambientD = wzLinearToSrgb(ambientLight.rgb);
		vec3 lightD = (visibility * diffuseD * (diffuseFactor * diffuseFactor) * 0.8 + ambientD * 0.2) * tileBrightness;
		lightD = blendAddEffectLighting(lightD, lightmap_vec4.rgb / 1.5f);
		return vec4(wzSrgbToLinear(lightD * albedo), 1.f);
	}
	vec3 diffuseTerm = (visibility*diffuseLight*(diffuseFactor*diffuseFactor)).rgb * 0.8;
	vec3 ambientTerm = ambientLight.rgb * 0.2;
	vec4 light = vec4(diffuseTerm + ambientTerm, ambientLight.a) * tileBrightness;
	light.rgb = blendAddEffectLighting(light.rgb, lightmap_vec4.rgb / 1.5f); // additive color (from environmental point lights / effects)
	light.a = 1.f;

	return light * vec4(albedo, 1.f);
}

// MOD (Wunsch 16.11.): Terrain strahlender - etwas heller, kraeftigere Farben, leicht mehr Kontrast (im Anzeige-Farbraum)
vec3 modGradeDisplay(vec3 c)
{
	c *= 1.12;
	float l = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(l), c, 1.25);
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
#ifdef NEWGL
	vec4 fragColor = (tile == MODV_TILE) ? vec4(modvColor(posModelSpace), 1.0) : modGrade(main_medium());   // MOD: Orbit-Leere
		// MOD (16.11. 18:06): Inselkacheln wie in MF - weiche Umrisslinie (schraege Ecken) mit hellem Rand; ausserhalb die Leere
	// (Felskoerper/Wolken), ebenso die schwarzen Stellen der MF-Kacheln (dort war in MF der Himmel)
	if (tile > MODV_TILE)
	{
		float modF = modvField(posModelSpace.xz);
		if (modF < 0.6)
		{
			float modRim = 1.0 - smoothstep(0.0, 0.05, abs(modF - 0.54));
			fragColor.rgb = mix(fragColor.rgb, vec3(0.86, 0.83, 0.72) * modvLight(), 0.5 * modRim);
		}
		vec3 modIc = texture2DArray(decalTex, vec3(uvDecal, float(tile)), WZ_MIP_LOAD_BIAS).rgb;
		if (linearLighting != 0) { modIc = wzLinearToSrgb(modIc); }
		float modK = 1.0 - smoothstep(0.035, 0.11, max(modIc.r, max(modIc.g, modIc.b)));
		modK = max(modK, 1.0 - smoothstep(0.48, 0.52, modF));
		if (modK > 0.0) { fragColor = mix(fragColor, vec4(modvColor(posModelSpace), 1.0), modK); }
	}
#else
	vec4 fragColor = modGrade(main_medium());
#endif

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
