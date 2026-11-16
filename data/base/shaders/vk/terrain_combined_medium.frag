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

vec3 getGroundUv(int i) {
	uint groundNo = fragf.grounds[i];
	return vec3(frag.uvGround * groundScale[groundNo/4u][groundNo%4u], groundNo);
}

vec3 getGround(int i) {
	vec3 color = texture(groundTex, getGroundUv(i), WZ_MIP_LOAD_BIAS).rgb;
	if (linearLighting != 0)
	{
		// The layer blend was tuned for display values, so re-encode each layer before the weighted sum
		color = wzLinearToSrgb(color);
	}
	return color * frag.groundWeights[i];
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 main_medium() {
	vec3 ground = getGround(0) + getGround(1) + getGround(2) + getGround(3);
	vec4 decal = fragf.tileNo >= 0 ? texture(decalTex, vec3(frag.uvDecal, fragf.tileNo), WZ_MIP_LOAD_BIAS) : vec4(0.f);
	if (linearLighting != 0)
	{
		// The decal mixes over the layers in display space as well
		decal.rgb = wzLinearToSrgb(decal.rgb);
	}

	vec3 L = normalize(frag.groundLightDir);
	vec3 N = vec3(0.f,0.f,1.f);
	float diffuseFactor = lambertTerm(N, L); // diffuse lighting

	float visibility = getShadowVisibility(frag.posModelSpace, frag.posViewSpace, diffuseFactor, 0.001f);

	vec4 lightmap_vec4 = texture(lightmap_tex, frag.uvLightmap);

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
	vec4 fragColor = (fragf.tileNo == MODV_TILE) ? vec4(modvColor(frag.posModelSpace), 1.0) : modGrade(main_medium());   // MOD: Orbit-Leere

	FragColor = fragColor;
}
