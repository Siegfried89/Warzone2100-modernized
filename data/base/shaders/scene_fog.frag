// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "view_position.glsl"
#include "distance_fog.glsl"
#include "srgb.glsl"

layout(std140) uniform cbuffer {
	vec4 fogColor;
	mat4 invProjectionMatrix;
	vec4 uvScaleClamp;
	float fogBegin;
	float fogEnd;
	float linearLighting;
	float padding1;
	vec4 dofParams;
	vec4 dofTexel;
};
uniform sampler2D sceneTexture;
uniform sampler2D prepassDepth;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex, uv) texture2D(tex, uv)
#endif

#ifdef NEWGL
in vec2 texCoords;
#else
varying vec2 texCoords;
#endif

#ifdef NEWGL
out vec4 FragColor;
#else
// Uses gl_FragColor
#endif

const float SKY_DEPTH_THRESHOLD = 0.9999;

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 lit = texture(sceneTexture, uv).rgb;
	vec3 result = lit;
	float depth = texture(prepassDepth, uv).r;
	if (depth < SKY_DEPTH_THRESHOLD)
	{
		float viewDist = length(wzGetViewPosition(texCoords, depth, invProjectionMatrix));
		// MOD: Schaerfentiefe - nur ausserhalb der Fokuszone, wenige Abtastungen (9), Radius waechst mit dem Abstand zur Fokuszone
		if (dofTexel.w > 0.0)
		{
			float farK = clamp((viewDist - dofParams.z) / max(dofParams.w, 1.0), 0.0, 1.0);
			float nearK = clamp((dofParams.y - viewDist) / max(dofParams.y * 0.5, 1.0), 0.0, 1.0);
			float coc = max(farK, nearK) * dofTexel.w;
			if (coc > 0.02)
			{
				float radius = coc * dofTexel.z;
				vec2 stepUv = dofTexel.xy * radius;
				vec2 diagUv = stepUv * 0.7071;
				vec3 sum = lit;
				sum += texture(sceneTexture, clamp(uv + vec2(stepUv.x, 0.0), vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv - vec2(stepUv.x, 0.0), vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv + vec2(0.0, stepUv.y), vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv - vec2(0.0, stepUv.y), vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv + diagUv, vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv - diagUv, vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv + vec2(diagUv.x, -diagUv.y), vec2(0.0), uvScaleClamp.zw)).rgb;
				sum += texture(sceneTexture, clamp(uv + vec2(-diagUv.x, diagUv.y), vec2(0.0), uvScaleClamp.zw)).rgb;
				lit = sum / 9.0;
				result = lit;
			}
		}
		float fogAmount = wzDistanceFogAmount(viewDist, fogBegin, fogEnd);
		if (linearLighting > 0.5)
		{
			// The fog fade was tuned for display values, so the mix runs in display space
			result = wzSrgbToLinear(mix(wzLinearToSrgb(lit), wzLinearToSrgb(fogColor.rgb), fogAmount));
		}
		else
		{
			result = mix(lit, fogColor.rgb, fogAmount);
		}
	}
	// Sky / no prepass geometry: skybox already applied its own fog in ScenePass.

	#ifdef NEWGL
	FragColor = vec4(result, 1.0);
	#else
	gl_FragColor = vec4(result, 1.0);
	#endif
}
