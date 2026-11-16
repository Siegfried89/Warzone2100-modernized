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
#include "pointlights.frag"

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
	float w = fgroundWeights[i];
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
#ifdef NEWGL

	if (tile > MODV_TILE) { lightmap_vec4.a = 1.0; }   // MOD: Orbit-Inseln - lightmap.a ist dort das Abstandsfeld, Helligkeit voll
#endif
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
	getGroundBM(0, bump);
	getGroundBM(1, bump);
	getGroundBM(2, bump);
	getGroundBM(3, bump);

	if (tile >= 0) {
		vec3 uv = vec3(uvDecal, float(tile));
		vec4 decalColor = texture2DArray(decalTex, uv, WZ_MIP_LOAD_BIAS);
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
