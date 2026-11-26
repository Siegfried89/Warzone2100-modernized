#version 450
//#pragma debug(on)

#include "tcmask_instanced.glsl"

layout (constant_id = 1) const uint WZ_SHADOW_MODE = 1;
layout (constant_id = 2) const uint WZ_SHADOW_FILTER_SIZE = 5;
layout (constant_id = 3) const uint WZ_SHADOW_CASCADES_COUNT = 3;
layout (constant_id = 4) const uint WZ_POINT_LIGHT_ENABLED = 0;

layout(set = 3, binding = 0) uniform sampler2D Texture; // diffuse
layout(set = 3, binding = 1) uniform sampler2D TextureTcmask; // tcmask
layout(set = 3, binding = 2) uniform sampler2D TextureNormal; // normal map
layout(set = 3, binding = 3) uniform sampler2D TextureSpecular; // specular map
layout(set = 3, binding = 4) uniform sampler2DArrayShadow shadowMap; // shadow map
layout(set = 3, binding = 5) uniform sampler2D lightmap_tex;

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 lightDir;
layout(location = 2) in vec3 halfVec;
layout(location = 3) in mat3 NormalMatrix; // local to model space for directional vectors
layout(location = 6) in mat3 TangentSpaceMatrix;
layout(location = 9) in vec4 colour;
layout(location = 10) in vec4 teamcolour;
layout(location = 11) in vec4 packed_ecmState_alphaTest_texCoord;
layout(location = 12) in vec3 uvLightmap; // uvLightmap in .xy, heightAboveTerrain in .z
// For Shadows
layout(location = 13) in vec3 posModelSpace;
layout(location = 14) in vec3 posViewSpace;
layout(location = 15) in vec3 posLocal; // MOD: Punkt im Modell (Wuestentarn)

layout(location = 0) out vec4 FragColor;

#include "pointlights.glsl"
#include "shadow_mapping.glsl"
#include "light.glsl"
#include "tangentspace.glsl"
#include "distance_fog.glsl"
#include "srgb.glsl"
#include "modcamo.glsl"

float random(vec2 uv)
{
	return fract(sin(dot(uv.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 blendAddEffectLighting(vec3 a, vec3 b) {
	return min(a + b, vec3(1.0));
}

vec4 applyShieldFuzzEffect(vec4 color) {
	float cycle = 0.66 + 0.66 * graphicsCycle;
	vec3 col = vec3(random(vec2(posModelSpace.x * cycle, posModelSpace.y * cycle)));
	col.r *= 0.5;
	col.g *= 0.5;
	col.b *= 2.0;
	return vec4(col, color.a / 6.0);
}

void main()
{
	// unpack inputs
	vec2 texCoord = vec2(packed_ecmState_alphaTest_texCoord.z, packed_ecmState_alphaTest_texCoord.w);
	bool ecmEffect = (packed_ecmState_alphaTest_texCoord.x > 0.f);
	bool alphaTest = (packed_ecmState_alphaTest_texCoord.y > 0.f);

	vec4 diffuseMap = texture(Texture, texCoord, WZ_MIP_LOAD_BIAS);

	if (alphaTest && (diffuseMap.a <= 0.5))
	{
		discard;
	}

	// Normal map implementations
	vec3 N = normal;
	if (normalmap != 0)
	{
		vec3 normalFromMap = texture(TextureNormal, texCoord, WZ_MIP_LOAD_BIAS).xyz;

		N = wzDecodeNormalMap(normalFromMap, hasTangents, TangentSpaceMatrix, NormalMatrix);
	}
	N = normalize(N);

	// Сalculate and combine final lightning
	vec3 L = normalize(lightDir);
	float diffuseFactor = lambertTerm(N, L); //diffuse light
	float visibility = getShadowVisibility(posModelSpace, posViewSpace, diffuseFactor, 0.0002f);
	diffuseFactor = min(diffuseFactor, visibility*diffuseFactor);
	visibility = mix(visibility, 1.0, (specularmap == 1) ? 1.0 : 0.0); // MOD (26.11.): 2 = Glanz nur fuer Reflexe (klassisch beleuchtet) // check to exclude double multiply for hq models
	vec4 lightmap_vec4 = texture(lightmap_tex, uvLightmap.xy, 0.f);
	float distanceAboveTerrain = uvLightmap.z;
	float lightmapFactor = 1.0f - (clamp(distanceAboveTerrain, 0.f, 300.f) / 300.f);
	// ambient light maxed for classic models to keep results similar to original
	float ambientBoost = (specularmap == 1) ? 1.0 : 2.0; // MOD (26.11.): Glanzkarte "_glint" (2) bleibt hell wie ein klassisches Modell

	float specularMapValue = 0.f;
	float gaussianTerm = 0.f;
	float modMirrorTerm = 0.f; // MOD (20.11.): Spiegelglanz (siehe unten)
	float vanillaFactor = 0.0; // Classic models shouldn't use diffuse light
	if (diffuseFactor > 0.0 && specularmap != 0)
	{
		specularMapValue = texture(TextureSpecular, texCoord, WZ_MIP_LOAD_BIAS).r;

		// Gaussian specular term computation
		vec3 H = normalize(halfVec);
		float exponent = acos(dot(H, N)) / 0.33; //0.33 is shininess
		gaussianTerm = exp(-(exponent * exponent));
		// MOD (Wunsch 20.11. "Die Specularmaps der Solarpanele deutlich kraeftiger machen ..., dass man deutliche Sonnenreflexionen sehen
		// kann"): Glanzkarten-Werte ueber 0,75 sind Spiegelflaechen (Solarzellen, blanker Stahl) - zusaetzlich zum breiten Glanz ein
		// scharfer, heller Sonnenreflex (bis 2,2-fach, Keulenbreite 0,11 statt 0,33). Werte bis 0,75 bleiben wie bisher.
		float mirror = clamp((specularMapValue - 0.75) * 4.0, 0.0, 1.0);
		float mirrorExp = acos(clamp(dot(H, N), -1.0, 1.0)) / 0.11;
		modMirrorTerm = mirror * 2.2 * exp(-(mirrorExp * mirrorExp));
		// MOD (20.11. nachmittags): glatte Spiegelflaechen haben eine ENGERE Glanzkeule - der breite Glanz wird dort schwaecher, sonst
		// werden Solarzellen ueber ~20 Grad Blickwinkel ganz weiss; so blitzen einzelne Zellen (Funkeln), der Rest bleibt blau
		gaussianTerm *= 1.0 - 0.75 * mirror;

		vanillaFactor = (specularmap == 1) ? 1.0 : 0.0; // Neutralize factor for spec map (MOD 26.11.: nicht bei "_glint")
	}

	// Calculate and combine final lighting
	vec4 light;
	float pointLightSpecularMap = specularMapValue;
	if (linearLighting != 0)
	{
		// The model lighting was tuned for display values, so it is evaluated in display space
		vec3 albedoD = wzLinearToSrgb(diffuseMap.rgb);
		vec3 lightD = wzLinearToSrgb(sceneColor.rgb);
		lightD += wzLinearToSrgb(specular.rgb) * gaussianTerm * diffuseFactor * specularMapValue;
		lightD += wzLinearToSrgb(specular.rgb) * modMirrorTerm * diffuseFactor;
		lightD += wzLinearToSrgb(diffuse.rgb) * diffuseFactor * albedoD * vanillaFactor;
		vec3 ambientTermD = blendAddEffectLighting(wzLinearToSrgb(ambient.rgb), ((lightmap_vec4.rgb * lightmapFactor) / 3.f));
		lightD += ambientTermD * albedoD * ambientBoost * visibility;
		light = vec4(wzSrgbToLinear(lightD), 1.0);
		// Like the albedo, the specular map scales display values, so convert it to linear the same way
		pointLightSpecularMap = wzSrgbToLinear(vec3(specularMapValue)).r;
	}
	else
	{
		light = sceneColor;
		light += specular * gaussianTerm * diffuseFactor * vec4(vec3(specularMapValue), 1.0);
		light += specular * modMirrorTerm * diffuseFactor * vec4(1.0, 1.0, 1.0, 0.0);
		light += diffuse * diffuseFactor * diffuseMap * vanillaFactor;
		vec3 ambientTerm = blendAddEffectLighting(ambient.rgb, ((lightmap_vec4.rgb * lightmapFactor) / 3.f));
		vec4 boostedAmbient = vec4(ambientTerm, ambient.a) * diffuseMap;
		boostedAmbient.rgb *= ambientBoost;
		light += boostedAmbient * visibility;
	}

	if (WZ_POINT_LIGHT_ENABLED == 1)
	{
		vec2 clipSpaceCoord = gl_FragCoord.xy / vec2(viewportWidth, viewportHeight);
		light += iterateOverAllPointLights(clipSpaceCoord, posModelSpace, N, normalize(normal), normalize(halfVec - lightDir), diffuseMap, pointLightSpecularMap);
	}

	light.a = 1.0f;

	vec4 fragColour;
	if (tcmask != 0)
	{
		// Get mask for team colors from texture
		float maskAlpha = texture(TextureTcmask, texCoord, WZ_MIP_LOAD_BIAS).r;
		// MOD (18.11.): Spielerfarbe Wuestentarn - Tarnmuster statt fester Teamfarbe (modcamo.glsl)
		vec4 tc = modCamoActive(teamcolour) ? modCamoColour(posLocal) : teamcolour;

		// Apply color using grain merge with tcmask
		if (linearLighting != 0)
		{
			// Grain merge (neutral at 0.5) and the color multiply also run in display space, as tuned
			vec4 blended = vec4(wzLinearToSrgb(light.rgb), light.a) + (tc - 0.5) * maskAlpha;
			fragColour = vec4(wzSrgbToLinear(max(blended.rgb, vec3(0.0)) * wzLinearToSrgb(colour.rgb)), blended.a * colour.a);
		}
		else
		{
			vec4 blended = light + (tc - 0.5) * maskAlpha;
			fragColour = blended * colour;
		}
	}
	else
	{
		fragColour = (linearLighting != 0)
			? vec4(wzSrgbToLinear(wzLinearToSrgb(light.rgb) * wzLinearToSrgb(colour.rgb)), light.a * colour.a)
			: light * colour;
	}

	if (ecmEffect)
	{
		fragColour.a = 0.66 + 0.66 * graphicsCycle;
	}

	if (shieldEffect != 0)
	{
		fragColour = applyShieldFuzzEffect(fragColour);
	}
	if (fogRange.z > 0.5 && fogOutput != WZ_FOG_OUTPUT_DISABLED)
	{
		float fogAmount = wzDistanceFogAmount(length(posViewSpace), fogRange.x, fogRange.y);
		fragColour.rgb = wzApplyForwardFog(fragColour.rgb, fragColour.a, fogAmount, fogColor.rgb, fogOutput, int(fogRange.w));
	}

	FragColor = fragColour;
}
