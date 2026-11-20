// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 20.11. 16:52): genaue Trefferflaeche der Einheiten - siehe modpick.h
#include "modpick.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pieclip.h"
#include "droid.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace modpick
{
namespace
{
constexpr float PAD = 2.0f;                     // Rand um die Dreiecke in Bildpunkten
constexpr float MIN_W = 256.0f / (3 * 330);     // wie pie_RotateProjectWithPerspective: davor liegt der Punkt hinter / in der Kamera

struct Part
{
	const iIMDShape *shape;
	glm::mat4 mvp;
};

struct Entry
{
	UDWORD frame = 0;
	std::vector<Part> parts;
};

std::unordered_map<uint32_t, Entry> s_entries;
Entry *s_current = nullptr;
glm::mat4 s_pv(1.f);
UDWORD s_lastSweep = 0;

void record(const iIMDShape *shape, const glm::mat4 &modelMatrix, void *)
{
	if (s_current && shape && !shape->polys.empty() && !shape->points.empty())
	{
		s_current->parts.push_back(Part{shape, s_pv * modelMatrix});
	}
}

struct Projected
{
	float x, y, iw;   // Bildpunkt und 1/Entfernung
	bool ok;
};

inline Projected project(const glm::mat4 &mvp, const Vector3f &p, float W, float H)
{
	const glm::vec4 v = mvp * glm::vec4(p.x, p.y, p.z, 1.f);
	if (v.w < MIN_W)
	{
		return Projected{0.f, 0.f, 0.f, false};
	}
	return Projected{(0.5f + 0.5f * v.x / v.w) * W, (0.5f - 0.5f * v.y / v.w) * H, 1.f / v.w, true};
}

inline float segDist2(float px, float py, float ax, float ay, float bx, float by)
{
	const float dx = bx - ax, dy = by - ay;
	const float len2 = dx * dx + dy * dy;
	float t = len2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
	t = std::max(0.f, std::min(1.f, t));
	const float ex = ax + t * dx - px, ey = ay + t * dy - py;
	return ex * ex + ey * ey;
}
}

void beginDroid(const DROID *psDroid, const glm::mat4 &perspectiveViewMatrix)
{
	const UDWORD frame = frameGetFrameNumber();
	// alte Eintraege (Einheit nicht mehr gezeichnet oder zerstoert) gelegentlich wegraeumen
	if (frame - s_lastSweep > 300)
	{
		s_lastSweep = frame;
		for (auto it = s_entries.begin(); it != s_entries.end();)
		{
			it = (frame - it->second.frame > 10) ? s_entries.erase(it) : std::next(it);
		}
	}
	Entry &e = s_entries[psDroid->id];
	e.frame = frame;
	e.parts.clear();
	s_current = &e;
	s_pv = perspectiveViewMatrix;
	pie_SetPickRecorder(record, nullptr);
}

void endDroid()
{
	pie_SetPickRecorder(nullptr, nullptr);
	s_current = nullptr;
}

Hit test(const DROID *psDroid, int mx, int my, float &depth, float &dist)
{
	depth = 0.f;
	dist = 0.f;
	const auto it = s_entries.find(psDroid->id);
	if (it == s_entries.end() || it->second.frame != psDroid->sDisplay.frameNumber || it->second.parts.empty())
	{
		return NO_DATA;
	}
	const float W = static_cast<float>(pie_GetVideoBufferWidth()), H = static_cast<float>(pie_GetVideoBufferHeight());
	const float px = static_cast<float>(mx) + 0.5f, py = static_cast<float>(my) + 0.5f;
	static std::vector<Projected> pts;   // nur im Anzeige-Thread benutzt

	bool exact = false;
	float bestDepth = 1e30f;
	float bestDist2 = PAD * PAD + 1e-3f;
	bool nearHit = false;
	float nearDepth = 1e30f;

	for (const Part &part : it->second.parts)
	{
		const iIMDShape *s = part.shape;
		// Grobpruefung: Huellquader des Modellteils im Bild
		{
			bool allOk = true;
			float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
			for (int c = 0; c < 8 && allOk; ++c)
			{
				const Vector3f corner((c & 1) ? s->max.x : s->min.x, (c & 2) ? s->max.y : s->min.y, (c & 4) ? s->max.z : s->min.z);
				const Projected q = project(part.mvp, corner, W, H);
				if (!q.ok) { allOk = false; break; }
				x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y);
			}
			if (allOk && (px < x0 - PAD || px > x1 + PAD || py < y0 - PAD || py > y1 + PAD))
			{
				continue;
			}
		}
		pts.resize(s->points.size());
		for (size_t k = 0; k < s->points.size(); ++k)
		{
			pts[k] = project(part.mvp, s->points[k], W, H);
		}
		for (const iIMDPoly &poly : s->polys)
		{
			if (poly.pindex[0] >= pts.size() || poly.pindex[1] >= pts.size() || poly.pindex[2] >= pts.size())
			{
				continue;
			}
			const Projected &a = pts[poly.pindex[0]], &b = pts[poly.pindex[1]], &c = pts[poly.pindex[2]];
			if (!a.ok || !b.ok || !c.ok)
			{
				continue;
			}
			// schneller Ausschluss ueber den Rahmen des Dreiecks
			if (px < std::min({a.x, b.x, c.x}) - PAD || px > std::max({a.x, b.x, c.x}) + PAD
			    || py < std::min({a.y, b.y, c.y}) - PAD || py > std::max({a.y, b.y, c.y}) + PAD)
			{
				continue;
			}
			const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
			if (std::abs(area) > 1e-4f)
			{
				const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area;
				const float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area;
				const float w2 = 1.f - w0 - w1;
				if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f)
				{
					const float iw = w0 * a.iw + w1 * b.iw + w2 * c.iw;
					const float d = iw > 0.f ? 1.f / iw : 1e29f;
					exact = true;
					bestDepth = std::min(bestDepth, d);
					continue;
				}
			}
			if (exact)
			{
				continue;   // ein echter Treffer ist schon da - Randtreffer zaehlen nicht mehr
			}
			const float d2 = std::min({segDist2(px, py, a.x, a.y, b.x, b.y), segDist2(px, py, b.x, b.y, c.x, c.y), segDist2(px, py, c.x, c.y, a.x, a.y)});
			if (d2 < bestDist2)
			{
				bestDist2 = d2;
				nearHit = true;
				nearDepth = 1.f / std::max(1e-9f, (a.iw + b.iw + c.iw) / 3.f);
			}
		}
	}
	if (exact)
	{
		depth = bestDepth;
		return EXACT;
	}
	if (nearHit)
	{
		depth = nearDepth;
		dist = std::sqrt(bestDist2);
		return NEAR;
	}
	return MISS;
}

void reset()
{
	s_entries.clear();
	s_current = nullptr;
	pie_SetPickRecorder(nullptr, nullptr);
}
}
