#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 ModelViewProjectionMatrix;
	mat4 ModelViewMatrix;
	vec4 shiftA;
	vec4 shiftB;
	vec4 params;
	vec4 params2;
	vec4 fogColor;
	vec4 fogRange;
};

layout(set = 1, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 1) uniform sampler2D tex1;
layout(set = 1, binding = 2) uniform sampler2D tex2;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 posViewSpace;
layout(location = 0) out vec4 FragColor;

#include "distance_fog.glsl"
#include "srgb.glsl"
#include "modshield_common.glsl"

void main()
{
	vec4 result;
	if (params.w < 0.5)
	{
		result = modShieldDome(texture(tex, uv), texture(tex, uv * 3.0 + shiftA.xy), texture(tex1, uv * 32.0 + shiftA.zw), texture(tex2, uv * 6.0 + shiftB.xy));
	}
	else
	{
		result = modShieldImpact(texture(tex, uv + shiftA.xy), texture(tex2, uv * 0.25), texture(tex2, uv).a);
	}
	FragColor = modShieldFinish(result, posViewSpace);
}
