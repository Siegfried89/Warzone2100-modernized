// Version directive is set by Warzone when loading the shader.
// MOD (18.11.): Schildkuppel / Einschlag wie Supreme Commander 1 (siehe modshield.frag).

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

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
in vec4 vertex;
in vec2 vertexTexCoord;
out vec2 uv;
out vec3 posViewSpace;
#else
attribute vec4 vertex;
attribute vec2 vertexTexCoord;
varying vec2 uv;
varying vec3 posViewSpace;
#endif

void main()
{
	uv = vertexTexCoord;
	gl_Position = ModelViewProjectionMatrix * vertex;
	posViewSpace = (ModelViewMatrix * vertex).xyz;
}
