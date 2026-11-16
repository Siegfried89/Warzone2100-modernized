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

void main()
{
	vec4 fragColor = (fragf.tileNo == MODV_TILE) ? vec4(modvColor(frag.posModelSpace), 1.0) : main_classic();   // MOD: Orbit-Leere

	FragColor = fragColor;
}
