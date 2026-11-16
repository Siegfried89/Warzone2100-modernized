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

// MOD: Orbit-Leere (Kachel 256) wie in Metal Fatigue - die Inseln schweben frei. Unter der Leere sieht man die Flanken der
// Inseln (zerklueftete Unterkante) und tief darunter den verwaschenen Planetenboden mit den Schatten der Inseln. Die Inselmaske
// steckt in lightmap.a (0 = Leere, gesetzt in src/terrain.cpp updateLightMap). Rein optisch: fuer das Spiel bleibt die Leere
// eine ebene, unpassierbare Flaeche auf Inselhoehe (keine Rampen, Flieger bleiben auf Hoehe).
const int MODV_TILE = 256;
const float MODV_TILE_SIZE = 128.0;
const float MODV_THICK = 640.0;     // Inseldicke (Weltmass, 1 Feld = 128)
const float MODV_DEPTH = 4600.0;    // so tief liegt der Boden unter den Inseln

bool modvIsland(vec2 xz)
{
	ivec2 sz = textureSize(lightmap_tex, 0);
	ivec2 t = ivec2(int(floor(xz.x / MODV_TILE_SIZE)), int(floor(-xz.y / MODV_TILE_SIZE)));
	if (t.x < 0 || t.y < 0 || t.x >= sz.x || t.y >= sz.y) { return false; }
	return texelFetch(lightmap_tex, t, 0).a > 0.0;
}

float modvHash(vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 modvColor(vec3 P)
{
	vec3 V = P - cameraPos.xyz;
	float down = max(-V.y, 0.05 * length(V));
	vec3 dir = V / down;   // Weg je Einheit Abstieg
	vec3 lightC = clamp(ambientLight.rgb * 0.55 + diffuseLight.rgb * 0.55, vec3(0.3), vec3(1.2));
	// 1. Flanke einer Insel hinter dem Rand
	for (int i = 1; i <= 16; ++i)
	{
		float d = MODV_THICK * float(i) / 16.0;
		vec3 q = P + dir * d;
		if (modvIsland(q.xz))
		{
			float bottom = MODV_THICK * (0.45 + 0.55 * modvHash(floor(q.xz / 48.0)));
			if (d <= bottom)
			{
				vec2 uv = vec2((q.x - q.z) / MODV_TILE_SIZE, d / MODV_TILE_SIZE);
				vec3 rock = textureLod(decalTex, vec3(fract(uv), float(MODV_TILE + 1)), 1.5).rgb;
				return rock * mix(0.9, 0.18, d / bottom) * lightC;
			}
		}
	}
	// 2. verwaschener Boden tief unten, mit Inselschatten
	vec3 g = P + dir * MODV_DEPTH;
	vec2 uvg = vec2(g.x, -g.z) / (MODV_TILE_SIZE * 4.0);
	vec3 col = textureLod(decalTex, vec3(fract(uvg), float(MODV_TILE)), 2.0).rgb;
	vec3 sd = normalize(sunPos.xyz);
	if (sd.y > 0.15)
	{
		float sh = 0.0;
		for (int s = 0; s < 4; ++s)
		{
			float up = MODV_DEPTH - MODV_THICK * (0.2 + 0.25 * float(s));
			vec3 r = g + sd * (up / sd.y);
			if (modvIsland(r.xz)) { sh += 0.25; }
		}
		col *= 1.0 - 0.45 * sh;
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

void main()
{
	vec4 fragColor = (fragf.tileNo == MODV_TILE) ? vec4(modvColor(frag.posModelSpace), 1.0) : main_bumpMapping();   // MOD: Orbit-Leere

	FragColor = fragColor;
}
