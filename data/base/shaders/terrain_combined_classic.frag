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
	float a = 1.0 - smoothstep(0.0, 54.0, d + n);
	if (a <= 0.001) { return decal; }
	vec4 c = modcSample(float(lay), w / 128.0);
	return vec4(mix(decal.rgb, c.rgb, a), max(decal.a, a));
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_classic() {
	int modcBaseTile = modcLayer(tile);
	vec4 decal = modcBaseTile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(modcBaseTile)), WZ_MIP_LOAD_BIAS) : vec4(0.f);
	decal = modcApply(decal, tile, posModelSpace);   // MOD (17.11.): weicher Betonrand / Wasserflaeche

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
