// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.40 - 1.50 core.)

#include "view_position.glsl"
#include "srgb.glsl"

layout(std140) uniform cbuffer {
	mat4 invProjectionMatrix;
	mat4 invViewMatrix;
	vec4 uvScaleClamp;
	vec4 sdfOriginExtent; // xy origin.xz, zw extent.xz (render world)
	vec4 sdfUvScaleClamp;
	vec4 sensorColor;
	vec4 weaponColor;
	vec4 minRangeColor;
	float fillAlpha;
	float sdfBand;
	float linearLighting;
	float padding2;
};
uniform sampler2D sceneTexture;
uniform sampler2D prepassDepth;
uniform sampler2D rangeRingSdf;

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
const float RING_HALF_PX = 2.0; // half-width of the smoothed ring, in pixels

// Cap pixel-width AA when world-XZ derivatives are highly anisotropic
// (silhouettes, depth jumps, map skirts) so fwidth does not paint a huge halo.
float overlayAaLimit(vec2 worldXZ, float band)
{
	float spanX = length(dFdx(worldXZ));
	float spanY = length(dFdy(worldXZ));
	float spanMax = max(spanX, spanY);
	float spanMin = min(spanX, spanY) + 1e-3;
	return (spanMax > 4.0 * spanMin) ? (band * 0.2) : band;
}

// Pixel-width smoothing of the SDF isocontour (sdf = 0).
// MOD (Wunsch 16.11. 21:42): Leuchtringe wie in Supreme Commander - duenner, heller Kern (fast weiss) plus weicher Lichthof zu beiden
// Seiten (SupCom zeichnet die Reichweiten als duenne Linie und laesst sie im Glow-Durchgang leuchten). Farben bleiben (Sensor blau,
// Waffe rot, Mindestreichweite gold). x = Kern (deckend), y = Lichthof (wird aufaddiert), z = Fuellung (deckend, Ringfarbe).
vec3 channelOverlay(float field, float band, float maxW, float wPix)
{
	float sd = (field * 2.0 - 1.0) * band; // decode [0, 1] -> [-band, +band]
	float uncovered = step(0.999, field);  // generate-pass clear = outside all rings
	float covered = 1.0 - uncovered;
	// fwidth(sd) ~ how much signed distance changes across this pixel.
	// MOD (17.11.): Pixelgroesse aus der Bodenposition statt aus der Ableitung des groben SDF-Rasters - dessen Ableitung springt von
	// Rasterfeld zu Rasterfeld; bei kleinen Ringen nah an der Kamera (Mindestreichweite, gold) blutete der Lichthof dadurch in
	// eckigen Bloecken in den Boden.
	float w = min(max(wPix, 1e-4), maxW);
	float px = abs(sd) / w;                // Abstand zur Ringlinie in Bildpunkten
	float ring = (1.0 - smoothstep(0.0, RING_HALF_PX * 0.8, px)) * covered;
	float fill = (1.0 - smoothstep(0.0, RING_HALF_PX, max(sd, 0.0) / w)) * fillAlpha * covered;
	// Lichthof: Gauss ueber ~9 Bildpunkte, laeuft vor dem Ende des SDF-Bands aus (sonst leuchtet die ganze Flaeche)
	float bandFade = 1.0 - smoothstep(0.55 * band, 0.95 * band, abs(sd));
	float glow = exp(-(px * px) / (2.0 * 9.0 * 9.0)) * 0.55 * bandFade * covered;
	return vec3(ring, glow, fill);
}

void main()
{
	vec2 uv = clamp(texCoords * uvScaleClamp.xy, vec2(0.0), uvScaleClamp.zw);
	vec3 lit = texture(sceneTexture, uv).rgb;
	vec3 result = lit;
	float depth = texture(prepassDepth, uv).r;
	// Bodenposition und ihre Pixelgroesse ausserhalb der Verzweigungen (Ableitungen nur im gleichmaessigen Kontrollfluss)
	vec3 viewPos = wzGetViewPosition(texCoords, min(depth, SKY_DEPTH_THRESHOLD), invProjectionMatrix);
	vec3 worldPos = (invViewMatrix * vec4(viewPos, 1.0)).xyz;
	float wPix = max(length(dFdx(worldPos.xz)), length(dFdy(worldPos.xz)));
	if (depth < SKY_DEPTH_THRESHOLD)
	{
		// Map terrain XZ into the frustum-fitted SDF atlas (same space as generate).
		vec2 sdfUv = (worldPos.xz - sdfOriginExtent.xy) / max(sdfOriginExtent.zw, vec2(1e-3));
		if (all(greaterThanEqual(sdfUv, vec2(0.0))) && all(lessThanEqual(sdfUv, vec2(1.0))))
		{
			sdfUv = clamp(sdfUv * sdfUvScaleClamp.xy, vec2(0.0), sdfUvScaleClamp.zw);
			vec3 field = texture(rangeRingSdf, sdfUv).rgb;
			float band = max(sdfBand, 1e-3);
			float maxW = overlayAaLimit(worldPos.xz, band);
			vec3 sensor = channelOverlay(field.r, band, maxW, wPix);
			vec3 weapon = channelOverlay(field.g, band, maxW, wPix);
			vec3 minRange = channelOverlay(field.b, band, maxW, wPix);
			// Kern heller (Richtung Weiss) wie eine leuchtende Linie, Hof in der Ringfarbe
			vec3 sC = mix(sensorColor.rgb, vec3(1.0), 0.35), wC = mix(weaponColor.rgb, vec3(1.0), 0.35), mC = mix(minRangeColor.rgb, vec3(1.0), 0.35);
			vec3 overlay = sC * sensor.x + wC * weapon.x + mC * minRange.x
			             + sensorColor.rgb * sensor.z * (1.0 - sensor.x) + weaponColor.rgb * weapon.z * (1.0 - weapon.x) + minRangeColor.rgb * minRange.z * (1.0 - minRange.x);
			vec3 glowAdd = sensorColor.rgb * sensor.y + weaponColor.rgb * weapon.y + minRangeColor.rgb * minRange.y;
			float a = clamp(max(max(max(sensor.x, sensor.z), max(weapon.x, weapon.z)), max(minRange.x, minRange.z)), 0.0, 1.0);
			overlay += glowAdd * (1.0 - a);
			if (linearLighting > 0.5)
			{
				// The ring colors were tuned for display values, so they are composed in display space
				result = wzSrgbToLinear(wzLinearToSrgb(lit) * (1.0 - a) + overlay);
			}
			else
			{
				result = lit * (1.0 - a) + overlay; // premultiplied-style overlay
			}
		}
	}

	#ifdef NEWGL
	FragColor = vec4(result, 1.0);
	#else
	gl_FragColor = vec4(result, 1.0);
	#endif
}
