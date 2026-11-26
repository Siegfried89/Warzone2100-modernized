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
// MOD (Wunsch 26.11. "fehlende Transitions, harte Schnitte bei den Tiles - fuer alles Uebergaenge, aber NICHT blurry, sondern schoen pixelig
// und stark, sodass es den Pixel-Look nicht verliert"):
//  - Boden: statt weicher Mischung der vier Ecken-Boeden zeigt jeder Block (16 je Feld) genau EINEN Boden - gewaehlt nach dem Ecken-Gewicht
//    mit kraeftigem Zufall je Block: die Boeden verzahnen sich gepixelt statt zu verschwimmen.
//  - Decals (Strassen, Krater, ...): an Feldern ohne Decal (Bits 24-27 der Decal-Nummer, terrain.cpp) laufen sie blockweise aus.
uint modtHash(ivec2 p)
{
	uint h = uint(p.x) * 73856093u ^ uint(p.y) * 19349663u;
	h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u;
	return h;
}
vec4 modtW = vec4(0.25);   // Boden-Gewichte (ein Boden je Block)
vec2 modtF = vec2(0.5);    // Lage im Feld (0..1), genau aus den Ecken-Gewichten
ivec2 modtBlk = ivec2(0);  // Weltblock (16 je Feld)
void modtSetup()
{
	vec4 w = fgroundWeights;
	modtF = clamp(vec2(w.z + w.w, w.y + w.z), 0.0, 1.0);
	vec2 tc = uvLightmap * vec2(textureSize(lightmap_tex, 0)) - 0.5;   // Weltlage in Feldern
	ivec2 tileO = ivec2(floor(tc - modtF + 0.5));
	vec2 bq = min(floor(modtF * 16.0), vec2(15.0));
	modtBlk = tileO * 16 + ivec2(bq);
	vec2 c = (bq + 0.5) / 16.0;
	vec4 wc = vec4((1.0 - c.x) * (1.0 - c.y), (1.0 - c.x) * c.y, c.x * c.y, c.x * (1.0 - c.y));
	uvec4 g = fgrounds;
	float bestS = -10.0;
	int bi = 0;
	for (int i = 0; i < 4; i++)
	{
		float tot = 0.0;
		for (int j = 0; j < 4; j++) { if (g[j] == g[i]) { tot += wc[j]; } }
		float n = float(modtHash(modtBlk + ivec2(int(g[i]) * 7919, int(g[i]) * 3571)) & 65535u) / 65535.0;
		float sc = tot + (n - 0.5) * 0.7;
		if (sc > bestS) { bestS = sc; bi = i; }
	}
	modtW = vec4(0.0);
	modtW[bi] = 1.0;
}
float modtDecalKeep(int flags)   // 1 = Decal zeigen, 0 = an dieser Stelle Boden (gepixelter Rand zu Feldern ohne Decal)
{
	if (flags == 0) { return 1.0; }
	float d = 2.0;
	if ((flags & 1) != 0) { d = min(d, modtF.y); }
	if ((flags & 2) != 0) { d = min(d, 1.0 - modtF.x); }
	if ((flags & 4) != 0) { d = min(d, 1.0 - modtF.y); }
	if ((flags & 8) != 0) { d = min(d, modtF.x); }
	const float BAND = 0.4;
	if (d >= BAND) { return 1.0; }
	float q = (floor(d * 16.0) + 0.5) / 16.0;
	float p = clamp(0.55 * (1.0 - q / BAND), 0.0, 0.6);
	return float(modtHash(modtBlk + ivec2(1777, 9029)) & 65535u) / 65535.0 < p ? 0.0 : 1.0;
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
#ifdef NEWGL
	return color * modtW[i];   // MOD (26.11.): ein Boden je Block
#else
	return color * fgroundWeights[i];
#endif
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_medium() {
#ifdef NEWGL
	modtSetup();
#endif
	vec3 ground = getGround(0) + getGround(1) + getGround(2) + getGround(3);
#ifdef NEWGL
	vec4 decal = tile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(tile & 0xFFFFFF)), WZ_MIP_LOAD_BIAS) : vec4(0.f);   // MOD (26.11.)
	decal.a *= tile >= 0 ? modtDecalKeep((tile >> 24) & 15) : 1.0;
#else
	vec4 decal = tile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(tile)), WZ_MIP_LOAD_BIAS) : vec4(0.f);
#endif
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
#else
	vec4 fragColor = modGrade(main_medium());
#endif

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
