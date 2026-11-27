// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: fertige Animationen fuer Ueber-Rumpfe (siehe modanimlib.h)

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "modanimlib.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace modanimlib
{
using namespace modcompbody;

namespace
{
struct Box
{
	glm::vec3 mn = glm::vec3(1e9f), mx = glm::vec3(-1e9f);
	bool ok = false;
	glm::vec3 centre() const { return (mn + mx) * 0.5f; }
	float vol() const { const glm::vec3 e = glm::max(mx - mn, glm::vec3(1.f)); return e.x * e.y * e.z; }
	bool holdsCentre() const { return mn.x <= 0.f && mx.x >= 0.f && mn.z <= 0.f && mx.z >= 0.f; }
};

const iIMDShape *shapeOf(const Part &p)
{
	const BodyInfo *b = bodyInfo(p.body);
	return (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
}

Box worldBox(const Part &p)
{
	Box b;
	const iIMDShape *m = shapeOf(p);
	if (m == nullptr) { return b; }
	const glm::mat4 M = partMatrix(p);
	for (int k = 0; k < 8; ++k)
	{
		const glm::vec3 c((k & 1) ? m->max.x : m->min.x, (k & 2) ? m->max.y : m->min.y, (k & 4) ? m->max.z : m->min.z);
		const glm::vec3 w = glm::vec3(M * glm::vec4(c, 1.f));
		b.mn = glm::min(b.mn, w); b.mx = glm::max(b.mx, w);
	}
	b.ok = true;
	return b;
}

/// Ende des Teils entlang seiner laengsten eigenen Achse: inner = das Ende naeher an der Rumpfmitte (waagerecht), sonst das hoehere Ende
glm::vec3 endPivot(const Part &p, bool inner)
{
	const iIMDShape *m = shapeOf(p);
	if (m == nullptr) { return glm::vec3(0.f); }
	const glm::vec3 lo(m->min.x, m->min.y, m->min.z), hi(m->max.x, m->max.y, m->max.z);
	const glm::vec3 c = (lo + hi) * 0.5f, e = hi - lo;
	const int ax = (e.x >= e.y && e.x >= e.z) ? 0 : (e.y >= e.z ? 1 : 2);
	glm::vec3 a = c, b = c;
	a[ax] = lo[ax]; b[ax] = hi[ax];
	const glm::mat4 M = partMatrix(p);
	const glm::vec3 wa = glm::vec3(M * glm::vec4(a, 1.f)), wb = glm::vec3(M * glm::vec4(b, 1.f));
	if (inner) { return (wa.x * wa.x + wa.z * wa.z) <= (wb.x * wb.x + wb.z * wb.z) ? a : b; }
	return wa.y >= wb.y ? a : b;
}

float pointBoxDist(const glm::vec3 &q, const Box &b)
{
	const glm::vec3 d = glm::max(glm::max(b.mn - q, q - b.mx), glm::vec3(0.f));
	return glm::length(d);
}

struct Leg
{
	int upper = -1, lower = -1;
	float ox = 0.f, oz = -1.f;     // waagerechte Richtung von der Mitte zum Fuss
	float ang = 0.f;               // Grad, 0 = vorn (-z), 90 = rechts
	float shin = 20.f;             // Laenge Knie - Fuss
	float reach = 20.f;            // waagerechter Abstand Huefte - Knie
	float phase = 0.f;             // Versatz im Gang (0..1)
};

struct Rig
{
	std::vector<Leg> legs;
	int body = -1;                 // Rumpfmitte (federt, schaukelt, Waffen sitzen meist darauf)
	std::vector<int> others;       // weitere Rumpfteile (haengen an der Rumpfmitte)
	float height = 40.f;
};

bool findRig(const Def &d, Rig &r, std::string &msg)
{
	const size_t n = d.parts.size();
	std::vector<Box> box(n);
	float ymin = 1e9f, ymax = -1e9f;
	for (size_t i = 0; i < n; ++i)
	{
		box[i] = worldBox(d.parts[i]);
		if (box[i].ok) { ymin = std::min(ymin, box[i].mn.y); ymax = std::max(ymax, box[i].mx.y); }
	}
	const float H = ymax - ymin;
	if (n < 2 || H < 2.f) { msg = "No legs found - build legs first: parts that stand on the ground, hanging on an upper part."; return false; }
	r.height = H;
	const float band = std::max(3.f, H * 0.15f);
	std::vector<bool> foot(n, false), used(n, false);
	int nFeet = 0;
	for (size_t i = 0; i < n; ++i) { if (box[i].ok && box[i].mn.y <= ymin + band) { foot[i] = true; ++nFeet; } }
	if (nFeet < 2 || nFeet >= static_cast<int>(n))
	{
		msg = "No legs found - this needs at least two parts standing on the ground (feet) and a body above them.";
		return false;
	}
	for (size_t i = 0; i < n; ++i)
	{
		if (!foot[i]) { continue; }
		Leg L;
		L.lower = static_cast<int>(i);
		const glm::vec3 fc = box[i].centre();
		const float hl = std::sqrt(fc.x * fc.x + fc.z * fc.z);
		if (hl > 0.5f) { L.ox = fc.x / hl; L.oz = fc.z / hl; }
		L.ang = glm::degrees(std::atan2(L.ox, -L.oz));
		L.shin = std::max(4.f, box[i].mx.y - box[i].mn.y);
		// Oberschenkel: Teil (kein Fuss, nicht die Rumpfmitte), das dem oberen Ende des Fusses am naechsten ist
		const glm::vec3 top(fc.x, box[i].mx.y, fc.z);
		float best = 6.f + 0.1f * H;
		for (size_t j = 0; j < n; ++j)
		{
			if (foot[j] || used[j] || !box[j].ok || box[j].holdsCentre()) { continue; }
			const glm::vec3 jc = box[j].centre();
			if (jc.x * L.ox + jc.z * L.oz <= 0.f) { continue; }   // muss auf der Seite des Beins liegen
			const float dd = pointBoxDist(top, box[j]);
			if (dd < best) { best = dd; L.upper = static_cast<int>(j); }
		}
		if (L.upper >= 0)
		{
			used[static_cast<size_t>(L.upper)] = true;
			const Part &u = d.parts[static_cast<size_t>(L.upper)];
			const glm::vec3 hip = glm::vec3(partMatrix(u) * glm::vec4(endPivot(u, true), 1.f));
			const glm::vec3 knee = glm::vec3(partMatrix(d.parts[i]) * glm::vec4(endPivot(d.parts[i], false), 1.f));
			L.reach = std::max(4.f, std::sqrt((knee.x - hip.x) * (knee.x - hip.x) + (knee.z - hip.z) * (knee.z - hip.z)));
		}
		r.legs.push_back(L);
	}
	// Rumpfmitte: Nicht-Bein-Teil ueber der Mitte (das groesste), sonst das naechste zur Mitte
	float bestVol = -1.f, bestDist = 1e9f;
	for (size_t i = 0; i < n; ++i)
	{
		if (foot[i] || used[i] || !box[i].ok) { continue; }
		const glm::vec3 c = box[i].centre();
		const float dist = std::sqrt(c.x * c.x + c.z * c.z);
		if (box[i].holdsCentre()) { if (box[i].vol() > bestVol) { bestVol = box[i].vol(); r.body = static_cast<int>(i); bestDist = -1.f; } }
		else if (bestVol < 0.f && dist < bestDist) { bestDist = dist; r.body = static_cast<int>(i); }
	}
	for (size_t i = 0; i < n; ++i)
	{
		if (!foot[i] && !used[i] && static_cast<int>(i) != r.body) { r.others.push_back(static_cast<int>(i)); }
	}
	return true;
}

/// Wert einer Kurve (Stuetzpunkte gleichmaessig ueber den Takt, dazwischen linear) an der Stelle ph (0..1, umlaufend)
float sample(const std::vector<float> &c, float ph)
{
	const size_t m = c.size();
	if (m == 0) { return 0.f; }
	ph -= std::floor(ph);
	const float x = ph * static_cast<float>(m);
	const size_t i = static_cast<size_t>(x) % m;
	const float f = x - std::floor(x);
	return c[i] * (1.f - f) + c[(i + 1) % m] * f;
}

struct GaitSpec
{
	int poses = 4;
	std::vector<float> hip, lift, knee, tuck;          // je Stuetzpunkt: Schwung vor(+1)/zurueck(-1), Heben, Knie vor/zurueck, Einklappen
	float aLift = 16.f, aYaw = 10.f, aKnee = 20.f, aKneeFrontExtra = 10.f, aTuck = 0.f;
	float stance = 0.5f;                               // Anteil am Boden
	bool wave = false;                                 // Beine reihum statt in zwei Gruppen
	std::vector<float> dip, roll, twist;               // Rumpfmitte je Stuetzpunkt (Weg / Grad, bei 45 Einheiten Hoehe)
};

GaitSpec spec(int g)
{
	GaitSpec s;
	if (g == GAIT_MONKEY)
	{
		s.poses = 6;
		s.hip = { 1.f, 0.33f, -0.33f, -1.f, 0.f, 0.8f };
		s.lift = { 0.f, -0.12f, -0.12f, 0.f, 1.f, 0.45f };
		s.knee = { 0.9f, 0.3f, -0.3f, -0.9f, -0.2f, 1.25f };
		s.tuck = { 0.f, 0.f, 0.f, 0.15f, 1.f, 0.1f };
		s.aLift = 28.f; s.aYaw = 22.f; s.aKnee = 26.f; s.aKneeFrontExtra = 0.f; s.aTuck = 34.f;
		s.dip = { -1.8f, -0.4f, 0.3f, -1.8f, -0.4f, 0.3f };
		s.roll = { 2.2f, 1.1f, -1.1f, -2.2f, -1.1f, 1.1f };
		s.twist = { 1.2f, 1.6f, 0.4f, -1.2f, -1.6f, -0.4f };
	}
	else if (g == GAIT_CRAWL)
	{
		s.poses = 8;
		s.hip = { 1.f, 0.66f, 0.33f, 0.f, -0.33f, -0.66f, -1.f, 0.f };
		s.lift = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f };
		s.knee = s.hip;
		s.tuck = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.6f };
		s.aLift = 18.f; s.aYaw = 12.f; s.aKnee = 22.f; s.aKneeFrontExtra = 0.f; s.aTuck = 20.f;
		s.stance = 0.75f; s.wave = true;
	}
	else
	{
		s.poses = 4;
		s.hip = { 1.f, 0.f, -1.f, 0.f };
		s.lift = { 0.f, 0.f, 0.f, 1.f };
		s.knee = s.hip;
		s.tuck = { 0.f, 0.f, 0.f, 0.f };
		s.dip = { -0.9f, 0.2f, -0.9f, 0.2f };
	}
	return s;
}

void clearKeys(Def &d)
{
	for (Part &p : d.parts)
	{
		for (auto &k : p.keys) { k.clear(); }
		p.motion = Motion();
	}
	for (int c = 0; c < CLIP_COUNT; ++c) { d.poseCount[c] = 0; }
}

void ensureKeys(Def &d, Part &p, int clip)
{
	p.keys[clip].resize(static_cast<size_t>(std::max(0, d.poseCount[clip])));
}
} // namespace

const char *gaitName(int g)
{
	static const char *const N[GAIT_COUNT] = { "Dragoon Walk", "Monkeylord Walk", "Crawler Walk" };
	return N[std::clamp(g, 0, GAIT_COUNT - 1)];
}

const char *gaitInfo(int g)
{
	static const char *const N[GAIT_COUNT] = { "legs in pairs", "high knees", "legs in turn" };
	return N[std::clamp(g, 0, GAIT_COUNT - 1)];
}

bool applyGait(Def &d, int gait, std::string &msg)
{
	Rig r;
	if (!findRig(d, r, msg)) { return false; }
	const GaitSpec s = spec(gait);
	clearKeys(d);
	for (Part &p : d.parts) { p.parent = -1; }
	d.poseCount[CLIP_MOVING] = s.poses;
	d.poseCount[CLIP_STANDING] = 0;
	d.poseCount[CLIP_FIRING] = 1;
	d.poseSeconds[CLIP_FIRING] = 0.45f;
	d.poseSeconds[CLIP_STANDING] = 3.f;
	const float sc = std::clamp(r.height / 45.f, 0.4f, 4.f);
	// Gruppen: reihum ums Fahrgestell abwechselnd (4 Beine: vorn/hinten gegen links/rechts, 6 Beine: Dreibein-Gang, 2 Beine: links/rechts)
	std::vector<size_t> order(r.legs.size());
	for (size_t i = 0; i < order.size(); ++i) { order[i] = i; }
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return r.legs[a].ang < r.legs[b].ang; });
	for (size_t k = 0; k < order.size(); ++k)
	{
		Leg &L = r.legs[order[k]];
		L.phase = s.wave ? static_cast<float>(k) / static_cast<float>(order.size()) : ((k % 2 == 0) ? 0.f : 0.5f);
	}
	float travel = 0.f;
	for (const Leg &L : r.legs)
	{
		const float side = std::fabs(L.ox);
		const float aKnee = s.aKnee + s.aKneeFrontExtra * (1.f - side);
		Part &low = d.parts[static_cast<size_t>(L.lower)];
		low.pivot = endPivot(low, false);   // Knie (oberes Ende); ohne Oberschenkel die Huefte
		ensureKeys(d, low, CLIP_MOVING);
		const bool hasUpper = L.upper >= 0;
		if (hasUpper)
		{
			Part &up = d.parts[static_cast<size_t>(L.upper)];
			up.pivot = endPivot(up, true);   // Huefte (Ende zur Rumpfmitte)
			low.parent = L.upper;
			ensureKeys(d, up, CLIP_MOVING);
			ensureKeys(d, up, CLIP_FIRING);
			up.keys[CLIP_FIRING][0].pos = glm::vec3(0.f, -0.4f, 1.2f) * sc;   // Rueckstoss federt mit
		}
		for (int k = 0; k < s.poses; ++k)
		{
			const float ph = static_cast<float>(k) / static_cast<float>(s.poses) + L.phase;
			const float h = sample(s.hip, ph), l = sample(s.lift, ph), kn = sample(s.knee, ph), t = sample(s.tuck, ph);
			PoseKey &lk = low.keys[CLIP_MOVING][static_cast<size_t>(k)];
			// Knie pendelt um die Querachse (Fuss vor/zurueck), Einklappen = Fuss zur Rumpfmitte
			lk.rot = glm::vec3(0.f, aKnee * kn + s.aTuck * t * L.oz, -s.aTuck * t * L.ox);
			if (hasUpper)
			{
				PoseKey &uk = d.parts[static_cast<size_t>(L.upper)].keys[CLIP_MOVING][static_cast<size_t>(k)];
				// Heben um die waagerechte Achse quer zum Bein; Schwingen um die Hochachse (nur der Teil, der nach vorn wirkt)
				uk.rot = glm::vec3(s.aYaw * h * L.ox, -s.aLift * l * L.oz, s.aLift * l * L.ox);
			}
			else
			{
				lk.pos = glm::vec3(0.f, 0.12f * L.shin * std::max(0.f, l), 0.f);   // ein Teil je Bein: das Bein hebt sich ein Stueck
			}
		}
		travel += 2.f * L.shin * std::sin(glm::radians(aKnee)) + (hasUpper ? 2.f * L.reach * std::sin(glm::radians(s.aYaw)) * side : 0.f);
	}
	travel /= static_cast<float>(std::max<size_t>(1, r.legs.size()));
	// Schrittweite passend zum Weg (Fuesse rutschen nicht): ein Takt = Weg, waehrend dessen jedes Bein einmal schiebt
	d.poseSeconds[CLIP_MOVING] = std::clamp(travel / s.stance / 128.f, 0.25f, 3.f);
	if (r.body >= 0)
	{
		Part &b = d.parts[static_cast<size_t>(r.body)];
		const iIMDShape *m = shapeOf(b);
		if (m != nullptr) { b.pivot = glm::vec3((m->min.x + m->max.x) * 0.5f, (m->min.y + m->max.y) * 0.5f, (m->min.z + m->max.z) * 0.5f); }
		ensureKeys(d, b, CLIP_MOVING);
		const size_t nL = std::max<size_t>(1, r.legs.size());
		for (int k = 0; k < s.poses; ++k)
		{
			const float ph = static_cast<float>(k) / static_cast<float>(s.poses);
			PoseKey &bk = b.keys[CLIP_MOVING][static_cast<size_t>(k)];
			if (!s.dip.empty()) { bk.pos.y = sample(s.dip, ph) * sc; }
			else { bk.pos.y = -0.5f * sc * (0.5f + 0.5f * std::cos(6.2831853f * static_cast<float>(nL) * ph)); }   // Krabbler: bei jedem Aufsetzen
			if (!s.twist.empty()) { bk.rot.x = sample(s.twist, ph); }
			if (!s.roll.empty()) { bk.rot.z = sample(s.roll, ph); }
		}
		ensureKeys(d, b, CLIP_FIRING);
		b.keys[CLIP_FIRING][0].rot = glm::vec3(0.f, -3.f, 0.f);
		b.keys[CLIP_FIRING][0].pos = glm::vec3(0.f, -0.6f, 2.2f) * sc;
		b.motion = defaultMotion(MOT_BOB);   // im Stand: atmet
		b.motion.when = WHEN_STANDING; b.motion.amount = 0.7f * sc; b.motion.speed = 0.35f;
		for (int o : r.others) { d.parts[static_cast<size_t>(o)].parent = r.body; }
	}
	char buf[160];
	snprintf(buf, sizeof(buf), "%s: %d legs found%s - steps follow the distance driven.", gaitName(gait), static_cast<int>(r.legs.size()),
	         r.body >= 0 ? ", body in the middle bounces" : "");
	msg = buf;
	return true;
}

const char *singleName(int s)
{
	static const char *const N[ONE_COUNT] = { "Spin (radar, dish)", "Hover bob", "Breathing", "Fire recoil", "Antenna sway" };
	return N[std::clamp(s, 0, ONE_COUNT - 1)];
}

const char *singleInfo(int s)
{
	static const char *const N[ONE_COUNT] = { "always", "moving", "standing", "firing", "always" };
	return N[std::clamp(s, 0, ONE_COUNT - 1)];
}

int singleClip(int s)
{
	switch (s)
	{
	case ONE_HOVER: return CLIP_MOVING;
	case ONE_RECOIL: return CLIP_FIRING;
	default: return CLIP_STANDING;
	}
}

bool applySingle(Def &d, int sel, int what, std::string &msg)
{
	if (d.parts.empty()) { msg = "Add a hull first."; return false; }
	int t = sel;
	if (t < 0 || t >= static_cast<int>(d.parts.size()))
	{
		Rig r;
		std::string dummy;
		t = (findRig(d, r, dummy) && r.body >= 0) ? r.body : 0;
	}
	Part &p = d.parts[static_cast<size_t>(t)];
	const bool had = p.motion.type != MOT_NONE && what != ONE_RECOIL;
	switch (what)
	{
	case ONE_SPIN: p.motion = defaultMotion(MOT_SPIN); p.pivot = pivotPreset(p, 1); break;
	case ONE_HOVER: p.motion = defaultMotion(MOT_BOB); p.motion.when = WHEN_MOVING; p.motion.amount = 1.5f; p.motion.speed = 1.2f; break;
	case ONE_BREATH: p.motion = defaultMotion(MOT_BOB); p.motion.when = WHEN_STANDING; p.motion.amount = 0.7f; p.motion.speed = 0.35f; break;
	case ONE_SWAY: p.motion = defaultMotion(MOT_SWAY); p.motion.when = WHEN_ALWAYS; p.motion.amount = 6.f; p.motion.speed = 0.6f; p.pivot = pivotPreset(p, 0); break;
	case ONE_RECOIL:
		if (d.poseCount[CLIP_FIRING] <= 0) { d.poseCount[CLIP_FIRING] = 1; d.poseSeconds[CLIP_FIRING] = 0.45f; }
		for (Part &q : d.parts) { ensureKeys(d, q, CLIP_FIRING); }
		p.keys[CLIP_FIRING][0].rot = glm::vec3(0.f, -3.f, 0.f);
		p.keys[CLIP_FIRING][0].pos = glm::vec3(0.f, -0.6f, 2.2f);
		break;
	default: return false;
	}
	msg = std::string(singleName(what)) + " on part " + std::to_string(t + 1) + (had ? " (replaced its old motion)" : "") + ".";
	return true;
}

void clearAll(Def &d)
{
	clearKeys(d);
}

bool applySet(Def &d, const Def &set, std::string &msg)
{
	if (d.parts.empty()) { msg = "Add a hull first."; return false; }
	clearKeys(d);
	const size_t n = std::min(d.parts.size(), set.parts.size());
	for (int c = 0; c < CLIP_COUNT; ++c) { d.poseCount[c] = set.poseCount[c]; d.poseSeconds[c] = set.poseSeconds[c]; }
	for (size_t i = 0; i < d.parts.size(); ++i)
	{
		Part &p = d.parts[i];
		if (i >= n) { p.parent = -1; continue; }
		const Part &q = set.parts[i];
		p.parent = (q.parent >= 0 && q.parent < static_cast<int>(d.parts.size())) ? q.parent : -1;
		p.pivot = q.pivot;
		p.motion = q.motion;
		for (int c = 0; c < CLIP_COUNT; ++c) { p.keys[c] = q.keys[c]; }
	}
	msg = "\"" + set.name + "\" put on the hull";
	if (set.parts.size() != d.parts.size())
	{
		msg += " - made for " + std::to_string(set.parts.size()) + " parts, this hull has " + std::to_string(d.parts.size()) + " (matched by number).";
	}
	else { msg += "."; }
	return true;
}

std::string describeKeys(const Part &p, int clip)
{
	float yaw = 0.f, tilt = 0.f, mv = 0.f;
	for (const PoseKey &k : p.keys[clip])
	{
		yaw = std::max(yaw, std::fabs(k.rot.x));
		tilt = std::max(tilt, std::max(std::fabs(k.rot.y), std::fabs(k.rot.z)));
		mv = std::max(mv, glm::length(k.pos));
	}
	std::string s;
	char b[32];
	if (yaw >= 0.5f) { snprintf(b, sizeof(b), "turn %.0f", yaw); s += b; }
	if (tilt >= 0.5f) { snprintf(b, sizeof(b), "%stilt %.0f", s.empty() ? "" : "  ", tilt); s += b; }
	if (mv >= 0.1f) { snprintf(b, sizeof(b), "%smove %.1f", s.empty() ? "" : "  ", mv); s += b; }
	return s.empty() ? std::string("still") : s;
}

std::string describeMotion(const Motion &m)
{
	std::string s = motionName(m.type);
	if (m.type != MOT_SPIN)
	{
		char b[24];
		snprintf(b, sizeof(b), " %.1f", m.amount);
		s += b;
	}
	return s + "  " + whenName(m.when);
}
}
