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
#endif

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_classic() {
	vec4 decal = tile >= 0 ? texture2DArray(decalTex, vec3(uvDecal, float(tile)), WZ_MIP_LOAD_BIAS) : vec4(0.f);

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

void main()
{
#ifdef NEWGL
	vec4 fragColor = (tile == MODV_TILE) ? vec4(modvColor(posModelSpace), 1.0) : main_classic();   // MOD: Orbit-Leere
#else
	vec4 fragColor = main_classic();
#endif

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
