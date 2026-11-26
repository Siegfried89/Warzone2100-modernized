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
#define WZ_POINT_LIGHT_ENABLED 0
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


in mat3 ModelTangentMatrix;
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
#include "pointlights.frag"

#ifdef NEWGL
// MOD (Wunsch 26.11. "fehlende Transitions, harte Schnitte bei den Tiles - fuer alles Uebergaenge, aber NICHT blurry, sondern schoen pixelig
// und stark, sodass es den Pixel-Look nicht verliert"; 17:31 "auch einzelne Pixel - fein auslaufen"):
//  - Boden: statt weicher Mischung der vier Ecken-Boeden zeigt jeder Texel (Bildpunkt in Kachel-Aufloesung) genau EINEN Boden - gewaehlt
//    nach dem Ecken-Gewicht plus einer Zufallsschwelle aus 8er-, 4er-, 2er- und 1er-Texel-Bloecken: grobe Klumpen, die zu einzelnen
//    Pixeln ausfransen.
//  - Decals (Strassen, Krater, Klippen, Beton ...): an Feldern ohne Decal oder mit Decal einer anderen Bodenart loesen sie sich zur Kante hin
//    gepixelt auf (Kanten N, O, S, W = Bits 24-27, Ecken NO, SO, SW, NW = Bits 16-19 der Decal-Nummer, terrain.cpp).
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
float modtThresh(ivec2 tx, int lvMin, int salt)
{
	float t = 0.10 * modtRand(tx >> max(0, lvMin), salt * 4);
	t += 0.15 * modtRand(tx >> max(1, lvMin), salt * 4 + 1);
	t += 0.25 * modtRand(tx >> max(2, lvMin), salt * 4 + 2);
	t += 0.50 * modtRand(tx >> max(3, lvMin), salt * 4 + 3);
	return t;
}
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
int modtLevel(vec2 gx, vec2 gy, float texN)
{
	float texelsPerPixel = max(max(length(gx), length(gy)) * texN, 1e-4);
	return clamp(int(ceil(log2(texelsPerPixel) - 0.25)), 0, 7);
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
vec4 modtW = vec4(0.25);   // Boden-Gewichte (ein Boden je Texel)
vec2 modtF = vec2(0.5);    // Mitte des Texel-Blocks im Feld (0..1)
ivec2 modtTx = ivec2(0);   // Welt-Texel
int modtLv = 0;            // feinste Stufe (Texel-Block nicht kleiner als ein Bildschirmpixel)
vec2 modtGx = vec2(0.0), modtGy = vec2(0.0);   // Ableitungen der Decal-UV (in modtSetup, ausserhalb jeder Verzweigung)
void modtSetup()
{
	modtGx = dFdx(uvDecal); modtGy = dFdy(uvDecal);
	vec4 w = fgroundWeights;
	vec2 f = clamp(vec2(w.z + w.w, w.y + w.z), 0.0, 1.0);   // Lage im Feld, genau aus den Ecken-Gewichten
	vec2 tc = uvLightmap * vec2(textureSize(lightmap_tex, 0)) - 0.5;   // Weltlage in Feldern
	ivec2 tileO = ivec2(floor(tc - f + 0.5));
	float texN = float(textureSize(decalTex, 0).x);
	vec2 dfx = dFdx(tc), dfy = dFdy(tc);
	modtLv = clamp(int(ceil(log2(max(max(length(dfx), length(dfy)) * texN, 1e-4)) - 0.25)), 0, 7);
	float bn = texN / exp2(float(modtLv));
	vec2 bq = min(floor(f * bn), vec2(bn - 1.0));
	modtF = (bq + 0.5) / bn;
	modtTx = tileO * int(texN) + ivec2(bq) * (1 << modtLv);
	vec2 c = modtF;
	vec4 wc = vec4((1.0 - c.x) * (1.0 - c.y), (1.0 - c.x) * c.y, c.x * c.y, c.x * (1.0 - c.y));
	uvec4 g = fgrounds;
	float bestS = -10.0;
	int bi = 0;
	for (int i = 0; i < 4; i++)
	{
		float tot = 0.0;
		for (int j = 0; j < 4; j++) { if (g[j] == g[i]) { tot += wc[j]; } }
		float sc = tot + (modtThresh(modtTx, modtLv, 20 + int(g[i])) - 0.5) * 1.1;
		if (sc > bestS) { bestS = sc; bi = i; }
	}
	modtW = vec4(0.0);
	modtW[bi] = 1.0;
}
float modtDecalKeep(int tileNo)   // 1 = Decal zeigen, 0 = hier Boden (gepixelte Aufloesung zu Feldern ohne passendes Decal)
{
	int flags = ((tileNo >> 24) & 15) | (((tileNo >> 16) & 15) << 4);
	if (flags == 0) { return 1.0; }
	float d = 9.0;
	for (int k = 0; k < 8; k++) { if ((flags & (1 << k)) != 0) { d = min(d, modtDist(modtF, k)); } }
	const float BAND = 0.45;
	if (d >= BAND) { return 1.0; }
	float wob = (modtRand(ivec2(floor(vec2(modtTx) / 42.0)), 3) - 0.5) * 0.2;
	float p = 1.0 - d / BAND - wob;   // an der Kante fast alles Boden, nach innen immer mehr Decal
	return modtThresh(modtTx, modtLv, 7) < p ? 0.0 : 1.0;
}
#endif

vec3 getGroundUv(int i) {
	uint groundNo = fgrounds[i];
	return vec3(uvGround * groundScale[groundNo/4u][groundNo%4u], float(groundNo));
}

struct BumpData {
	vec4 color;
	vec3 N;
	float gloss;
};

void getGroundBM(int i, inout BumpData res) {
	vec3 uv = getGroundUv(i);
#ifdef NEWGL
	float w = modtW[i];   // MOD (26.11.): ein Boden je Block
#else
	float w = fgroundWeights[i];
#endif
	vec4 color = texture2DArray(groundTex, uv, WZ_MIP_LOAD_BIAS);
	if (linearLighting != 0)
	{
		// The layer blend was tuned for display values, so re-encode each layer before the weighted sum
		color.rgb = wzLinearToSrgb(color.rgb);
	}
	res.color += color * w;
	vec3 N = texture2DArray(groundNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
	N = mix(normalize(N * 2.f - 1.f), vec3(0.f,0.f,1.f), vec3(float(N == vec3(0.f,0.f,0.f))));
	res.N += N * w;
	res.gloss += texture2DArray(groundSpecular, uv, WZ_MIP_LOAD_BIAS).r * w;
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return a + b;
}

vec4 doBumpMapping(BumpData b, vec3 groundLightDir, vec3 groundHalfVec) {
	vec3 L = normalize(groundLightDir);
	float diffuseFactor = lambertTerm(b.N, L); // diffuse lighting
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, diffuseFactor, 0.001f);
	diffuseFactor = min(diffuseFactor, visibility*diffuseFactor);

	float specularFactor = blinnTerm(b.N, normalize(groundHalfVec), b.gloss, 16.f);

	vec4 lightmap_vec4 = texture(lightmap_tex, uvLightmap, 0.f);
	float adjustedTileBrightness = pow(lightmap_vec4.a, 2.f-lightmap_vec4.a); // ... * tile brightness / ambient occlusion (stored in lightmap.a)
	vec4 res;
	vec4 albedoLinear = b.color;
	float pointLightGloss = b.gloss;
	if (linearLighting != 0)
	{
		// The tier's lighting was tuned for display values, so it is evaluated in display space
		vec3 ambientD = wzLinearToSrgb(ambientLight.rgb);
		vec3 diffuseD = wzLinearToSrgb(diffuseLight.rgb);
		vec3 specularD = wzLinearToSrgb(specularLight.rgb);
		vec3 lightD = ambientD * adjustedTileBrightness + diffuseD * diffuseFactor + lightmap_vec4.rgb / 1.4f;
		vec3 specD = (specularD * specularFactor * diffuseFactor + lightmap_vec4.rgb / 2.5f) * (b.gloss * b.gloss);
		res = vec4(wzSrgbToLinear(b.color.rgb * lightD + specD), b.color.a);
		albedoLinear.rgb = wzSrgbToLinear(b.color.rgb);
		// Like the albedo, the gloss map scales display values, so convert it to linear the same way
		pointLightGloss = wzSrgbToLinear(vec3(b.gloss)).r;
	}
	else
	{
		vec4 adjustedAmbientLight = ambientLight * adjustedTileBrightness;
		vec4 light = adjustedAmbientLight + diffuseLight * diffuseFactor;
		light.rgb = blendAddEffectLighting(light.rgb, lightmap_vec4.rgb / 1.4f); // additive color (from environmental point lights / effects)

		vec4 light_spec = specularLight * specularFactor * diffuseFactor;
		light_spec.rgb = blendAddEffectLighting(light_spec.rgb, lightmap_vec4.rgb / 2.5f); // additive color (from environmental point lights / effects)
		light_spec *= (b.gloss * b.gloss);

		res = (b.color*light) + light_spec;
	}

#if WZ_POINT_LIGHT_ENABLED == 1
	// point lights
	vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(float(viewportWidth), float(viewportHeight));
	// The light loop works in world space, so the tangent frame is inverted once here.
	// The frame is built orthonormal, so its transpose stands in for its inverse, and the third column of that is the vertex normal.
	mat3 tangentToWorld = transpose(ModelTangentMatrix);
	res += iterateOverAllPointLights(clipSpaceCoord, posModelSpace, tangentToWorld * b.N, tangentToWorld[2], tangentToWorld * normalize(groundHalfVec - groundLightDir), albedoLinear, pointLightGloss);
#endif

	// Calculate water murkiness based on non-constant-density-fog, see https://iquilezles.org/articles/fog/
	vec3 c2p = normalize(posModelSpace - cameraPos.xyz); // camera to point vector
	vec3 c = normalize(cameraPos.xyz);
	float inscatter = 0.007; // in-scattering
	float extinction = 0.001; // extinction
	float murkyFactor = exp(-c.y * extinction) * (1.0 - exp( -(posModelSpace.y+40.0) * c2p.y * extinction)) / (-c2p.y * extinction);
	float murkiness = inscatter * murkyFactor;
	float waterDeep = 0.003 * murkyFactor;
	murkiness = clamp(murkiness, 0.0, 1.0);
	waterDeep = clamp(waterDeep, 0.0, 1.0);
	// The murk color, its depth fade and the tile attenuation in lightmap.a were tuned for display values,
	// so under linear lighting they apply in display space
	vec3 murkColor = vec3(0.315,0.425,0.475)*vec3(1.0-waterDeep);
	if (linearLighting != 0)
	{
		vec3 display = mix(wzLinearToSrgb(res.rgb), murkColor, murkiness) * lightmap_vec4.a;
		return vec4(wzSrgbToLinear(display), b.color.a);
	}
	res.rgb = mix(res.rgb, murkColor, murkiness);
	return vec4(res.rgb * lightmap_vec4.a, b.color.a);
}

vec4 main_bumpMapping() {
	BumpData bump;
	bump.color = vec4(0.f);
	bump.N = vec3(0.f);
	bump.gloss = 0.f;
#ifdef NEWGL
	modtSetup();
#endif
	getGroundBM(0, bump);
	getGroundBM(1, bump);
	getGroundBM(2, bump);
	getGroundBM(3, bump);

	if (tile >= 0) {
#ifdef NEWGL
		vec3 uv = vec3(uvDecal, float(tile & 0xFFFF));   // MOD (26.11.): Bits 16-27 = Kanten/Ecken ohne passendes Decal
#else
		vec3 uv = vec3(uvDecal, float(tile));
#endif
		vec4 decalColor = texture2DArray(decalTex, uv, WZ_MIP_LOAD_BIAS);
#ifdef NEWGL
		decalColor = modtAlpha(decalColor, uv.z, uv.xy, modtGx, modtGy);   // (26.11.) gepixelte Deckkraft
		decalColor.a *= modtDecalKeep(tile);
#endif
		if (linearLighting != 0)
		{
			// The decal mixes over the layers in display space as well
			decalColor.rgb = wzLinearToSrgb(decalColor.rgb);
		}
		float a = decalColor.a;
		// blend color, normal and gloss with ground ones based on alpha
		bump.color = (1.f - a)*bump.color + a*vec4(decalColor.rgb, 1.f);
		vec3 n = texture2DArray(decalNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
		vec3 n_normalized = normalize(n * 2.f - 1.f);
		n = mix(vec3(n_normalized.xy * decal2groundMat2, n_normalized.z), vec3(0.f,0.f,1.f), vec3(float(n == vec3(0.f,0.f,0.f))));
		bump.N = (1.f - a)*bump.N + a*n;
		bump.gloss = (1.f - a)*bump.gloss + a*texture2DArray(decalSpecular, uv, WZ_MIP_LOAD_BIAS).r;
	}
	// Every lighting term below assumes unit length - but a weighted sum of unit normals is shorter than unit when they disagree (so normalize)
	bump.N = normalize(bump.N);
	return doBumpMapping(bump, groundLightDir, groundHalfVec);
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
	vec4 fragColor = (tile == MODV_TILE) ? vec4(modvColor(posModelSpace), 1.0) : modGrade(main_bumpMapping());   // MOD: Orbit-Leere
#else
	vec4 fragColor = modGrade(main_bumpMapping());
#endif

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
