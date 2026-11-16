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
const float MODV_DEPTH = 4600.0;         // so tief liegt der Boden unter den Inseln
const float MODV_FLOOR_REPEAT = 2048.0;  // Wiederholung der Bodentextur (Welteinheiten)

// Feldgroesse in Welteinheiten aus der Lichtkarten-Matrix (1 / (Feldgroesse * Lichtkartenbreite)) - gilt fuer jedes Raster (128 oder 64)


// weiches Abstandsfeld (in Feldern) an einer Stelle (Modellkoordinaten x/z)




// Schwelle des Sockels in der Tiefe d (oben 0.5 = Umrisslinie): oben fast senkrecht, unten rund eingezogen (wie in MF), unregelmaessig

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


vec3 modvColor(vec3 P)
{
	// MOD (Wunsch 16.11. 23:13): Orbit wie in MF - unter der Leere nur der verschwommene Planetenboden tief unten. Die Inseln sind die
	// echten MF-Asteroiden-Modelle (Engine, display3d). Der fruehere nachgebaute Felssockel samt Inselrand ist komplett entfernt.
	vec3 V = P - cameraPos.xyz;
	float lenV = max(length(V), 1.0);
	float down = max(-V.y, 0.02 * lenV);
	vec3 dir = V / down;                 // Weg je Einheit Abstieg
	float cosI = down / lenV;            // Steilheit des Blicks (1 = senkrecht)
	vec3 lightC = modvLight();
	vec3 haze = modvHaze(lightC);
	vec3 g = P + dir * MODV_DEPTH;
	float distG = lenV + length(dir) * MODV_DEPTH;
	vec2 uvg = vec2(g.x, -g.z) / MODV_FLOOR_REPEAT;
	vec3 col = textureLod(decalTex, vec3(fract(uvg), float(MODV_TILE)), modvLod(distG, cosI, MODV_FLOOR_REPEAT) + 2.5).rgb;
	return mix(col * lightC, haze, 0.15 + 0.85 * smoothstep(9000.0, 32000.0, distG));
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
