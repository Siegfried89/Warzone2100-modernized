// Version directive is set by Warzone when loading the shader.

layout(std140) uniform cbuffer {
	mat4 ModelViewProjectionMatrix;
	mat4 ModelViewMatrix;
	vec4 shiftA;
	vec4 shiftB;
	vec4 params;
	vec4 params2;
	vec4 fogColor;
	vec4 fogRange;
};

uniform sampler2D tex;
uniform sampler2D tex1;
uniform sampler2D tex2;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec2 uv;
in vec3 posViewSpace;
out vec4 FragColor;
#define WZ_TEX texture
#else
varying vec2 uv;
varying vec3 posViewSpace;
#define FragColor gl_FragColor
#define WZ_TEX texture2D
#endif

#include "distance_fog.glsl"
#include "srgb.glsl"
#include "modshield_common.glsl"

void main()
{
	vec4 result;
	if (params.w < 0.5)
	{
		result = modShieldDome(WZ_TEX(tex, uv), WZ_TEX(tex, uv * 3.0 + shiftA.xy), WZ_TEX(tex1, uv * 32.0 + shiftA.zw), WZ_TEX(tex2, uv * 6.0 + shiftB.xy));
	}
	else
	{
		result = modShieldImpact(WZ_TEX(tex, uv + shiftA.xy), WZ_TEX(tex2, uv * 0.25), WZ_TEX(tex2, uv).a);
	}
	FragColor = modShieldFinish(result, posViewSpace);
}
