#version 450
// MOD (18.11.): Schildkuppel / Einschlag wie Supreme Commander 1 (siehe modshield.frag).

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

layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 vertexTexCoord;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 posViewSpace;

void main()
{
	uv = vertexTexCoord;
	gl_Position = ModelViewProjectionMatrix * vertex;
	posViewSpace = (ModelViewMatrix * vertex).xyz;
	gl_Position.y *= -1.;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
