#version 450

#include "terrain_combined.glsl"
#include "srgb.glsl"

layout (constant_id = 1) const uint WZ_SHADOW_MODE = 1;
layout (constant_id = 2) const uint WZ_SHADOW_FILTER_SIZE = 5;
layout (constant_id = 3) const uint WZ_SHADOW_CASCADES_COUNT = 3;
layout (constant_id = 4) const uint WZ_POINT_LIGHT_ENABLED = 0;

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
layout(location = 12) in mat3 ModelTangentMatrix;

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
#include "pointlights.glsl"

vec3 getGroundUv(int i) {
	uint groundNo = fragf.grounds[i];
	return vec3(frag.uvGround * groundScale[groundNo/4u][groundNo%4u], groundNo);
}

struct BumpData {
	vec4 color;
	vec3 N;
	float gloss;
};

void getGroundBM(int i, inout BumpData res) {
	vec3 uv = getGroundUv(i);
	float w = frag.groundWeights[i];
	vec4 color = texture(groundTex, uv, WZ_MIP_LOAD_BIAS);
	if (linearLighting != 0)
	{
		// The layer blend was tuned for display values, so re-encode each layer before the weighted sum
		color.rgb = wzLinearToSrgb(color.rgb);
	}
	res.color += color * w;
	vec3 N = texture(groundNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
	N = mix(normalize(N * 2.f - 1.f), vec3(0.f,0.f,1.f), vec3(float(N == vec3(0.f,0.f,0.f))));
	res.N += N * w;
	res.gloss += texture(groundSpecular, uv, WZ_MIP_LOAD_BIAS).r * w;
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return a + b;
}

vec4 doBumpMapping(BumpData b, vec3 groundLightDir, vec3 groundHalfVec) {
	vec3 L = normalize(groundLightDir);
	float diffuseFactor = lambertTerm(b.N, L); // diffuse lighting
	float visibility = getShadowVisibility(frag.posModelSpace, frag.posViewSpace, diffuseFactor, 0.001f);
	diffuseFactor = min(diffuseFactor, visibility*diffuseFactor);

	float specularFactor = blinnTerm(b.N, normalize(groundHalfVec), b.gloss, 16.f);

	vec4 lightmap_vec4 = texture(lightmap_tex, frag.uvLightmap, 0.f);

	if (fragf.tileNo > MODV_TILE) { lightmap_vec4.a = 1.0; }   // MOD: Orbit-Inseln - lightmap.a ist dort das Abstandsfeld, Helligkeit voll
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

	if (WZ_POINT_LIGHT_ENABLED == 1)
	{
		// point lights
		vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(viewportWidth, viewportHeight);
		// The light loop works in world space, so the tangent frame is inverted once here.
		// The frame is built orthonormal, so its transpose stands in for its inverse, and the third column of that is the vertex normal.
		mat3 tangentToWorld = transpose(ModelTangentMatrix);
		res += iterateOverAllPointLights(clipSpaceCoord, frag.posModelSpace, tangentToWorld * b.N, tangentToWorld[2], tangentToWorld * normalize(groundHalfVec - groundLightDir), albedoLinear, pointLightGloss);
	}

	// Calculate water murkiness based on non-constant-density-fog, see https://iquilezles.org/articles/fog/
	vec3 c2p = normalize(frag.posModelSpace - cameraPos.xyz); // camera to point vector
	vec3 c = normalize(cameraPos.xyz);
	float inscatter = 0.007; // in-scattering
	float extinction = 0.001; // extinction
	float murkyFactor = exp(-c.y * extinction) * (1.0 - exp( -(frag.posModelSpace.y+40.0) * c2p.y * extinction)) / (-c2p.y * extinction);
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
	bump.color = vec4(0);
	bump.N = vec3(0);
	bump.gloss = 0;
	getGroundBM(0, bump);
	getGroundBM(1, bump);
	getGroundBM(2, bump);
	getGroundBM(3, bump);

	if (fragf.tileNo >= 0) {
		vec3 uv = vec3(frag.uvDecal, fragf.tileNo);
		vec4 decalColor = texture(decalTex, uv, WZ_MIP_LOAD_BIAS);
		if (linearLighting != 0)
		{
			// The decal mixes over the layers in display space as well
			decalColor.rgb = wzLinearToSrgb(decalColor.rgb);
		}
		float a = decalColor.a;
		// blend color, normal and gloss with ground ones based on alpha
		bump.color = (1-a)*bump.color + a*vec4(decalColor.rgb, 1);
		vec3 n = texture(decalNormal, uv, WZ_MIP_LOAD_BIAS).xyz;
		vec3 n_normalized = normalize(n * 2.f - 1.f);
		n = mix(vec3(n_normalized.xy * frag.decal2groundMat2, n_normalized.z), vec3(0.f,0.f,1.f), vec3(float(n == vec3(0.f,0.f,0.f))));
		bump.N = (1-a)*bump.N + a*n;
		bump.gloss = (1-a)*bump.gloss + a*texture(decalSpecular, uv, WZ_MIP_LOAD_BIAS).r;
	}
	// Every lighting term below assumes unit length - but a weighted sum of unit normals is shorter than unit when they disagree (so normalize)
	bump.N = normalize(bump.N);
	return doBumpMapping(bump, frag.groundLightDir, frag.groundHalfVec);
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
	vec4 fragColor = (fragf.tileNo == MODV_TILE) ? vec4(modvColor(frag.posModelSpace), 1.0) : modGrade(main_bumpMapping());   // MOD: Orbit-Leere
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
