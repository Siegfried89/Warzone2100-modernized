// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (22.11.): saubere leuchtende Baender fuer Schlachtplan, Angriffsplaner und Karte (siehe modglow.h)
#include "modglow.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/pieblitfunc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace modglow
{
namespace
{
struct RGBA
{
	uint8_t r, g, b, a;
};

RGBA mix(const Colour &c, float toWhite, float alpha)
{
	const auto ch = [toWhite](unsigned char v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(v + (255 - v) * toWhite), 0, 255)); };
	return RGBA{ ch(c.r), ch(c.g), ch(c.b), static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255)) };
}

float cross(const glm::vec2 &a, const glm::vec2 &b) { return a.x * b.y - a.y * b.x; }

// Dreiecksstreifen mit entarteten Verbindungen zwischen den Teilstreifen. Jeder Teilstreifen beginnt auf einer geraden Stelle, damit alle
// seine Dreiecke dieselbe Umlaufrichtung haben wie das erste (die Pipeline verwirft Rueckseiten).
struct Mesh
{
	std::vector<float> xy;
	std::vector<uint8_t> rgba;
	glm::vec2 lastP{ 0.f, 0.f };
	RGBA lastC{ 0, 0, 0, 0 };

	size_t count() const { return xy.size() / 2; }
	void put(const glm::vec2 &p, const RGBA &c)
	{
		xy.push_back(p.x); xy.push_back(p.y);
		rgba.push_back(c.r); rgba.push_back(c.g); rgba.push_back(c.b); rgba.push_back(c.a);
		lastP = p; lastC = c;
	}
	void begin(const glm::vec2 &first, const RGBA &c)
	{
		if (count() == 0) { return; }
		const glm::vec2 lp = lastP;
		const RGBA lc = lastC;
		put(lp, lc);
		put(first, c);
		if (count() % 2 != 0) { put(first, c); }
	}
	/// Teilstreifen aus Paaren (a_i, b_i); die Reihenfolge im Paar wird so gewaehlt, dass das erste Dreieck nach vorne zeigt
	void band(const std::vector<glm::vec2> &a, const std::vector<RGBA> &ca, const std::vector<glm::vec2> &b, const std::vector<RGBA> &cb)
	{
		const size_t n = std::min(a.size(), b.size());
		if (n < 2) { return; }
		// erstes nicht entartetes Dreieck pruefen
		bool swap = false;
		for (size_t i = 0; i + 1 < n; ++i)
		{
			const float cr = cross(b[i] - a[i], a[i + 1] - a[i]);
			if (std::fabs(cr) > 1e-3f) { swap = cr < 0.f; break; }
		}
		const std::vector<glm::vec2> &p0 = swap ? b : a;
		const std::vector<glm::vec2> &p1 = swap ? a : b;
		const std::vector<RGBA> &c0 = swap ? cb : ca;
		const std::vector<RGBA> &c1 = swap ? ca : cb;
		begin(p0[0], c0[0]);
		for (size_t i = 0; i < n; ++i) { put(p0[i], c0[i]); put(p1[i], c1[i]); }
	}
	void tri(glm::vec2 a, glm::vec2 b, glm::vec2 c, RGBA ca, RGBA cb, RGBA cc)
	{
		if (cross(b - a, c - a) < 0.f) { std::swap(b, c); std::swap(cb, cc); }
		begin(a, ca);
		put(a, ca); put(b, cb); put(c, cc);
	}
};

std::vector<std::unique_ptr<GFX>> s_pool;
size_t s_used = 0;
size_t s_frame = static_cast<size_t>(-1);
constexpr size_t POOL_MAX = 768;   // Sicherheitsgrenze je Bild

void flush(Mesh &m, bool additive = false)
{
	if (m.count() < 3) { return; }
	// auf eine Zweierpotenz auffuellen (entartete Dreiecke): gleichbleibende Puffergroesse, Vulkan legt den Puffer dann nicht jedes Bild neu an
	size_t cap = 64;
	while (cap < m.count()) { cap *= 2; }
	while (m.count() < cap) { const glm::vec2 lp = m.lastP; const RGBA lc = m.lastC; m.put(lp, lc); }
	// jeder Puffer wird hoechstens einmal je Bild beschrieben (Vulkan kopiert alle Uploads vor dem Zeichnen) - daher ein Vorrat
	const size_t f = gfx_api::context::get().current_FrameNum();
	if (f != s_frame) { s_frame = f; s_used = 0; }
	if (s_used >= POOL_MAX) { return; }
	if (s_used >= s_pool.size()) { s_pool.push_back(std::make_unique<GFX>(GFX_COLOUR, 2)); }
	GFX *g = s_pool[s_used++].get();
	g->buffers(static_cast<int>(m.count()), m.xy.data(), m.rgba.data());
	if (additive)
	{
		gfx_api::ModGlowAdditivePSO::get().bind();
		g->draw<gfx_api::ModGlowAdditivePSO>(defaultProjectionMatrix());
	}
	else
	{
		gfx_api::RadarViewInsideFillPSO::get().bind();
		g->draw<gfx_api::RadarViewInsideFillPSO>(defaultProjectionMatrix());
	}
}

// Querschnitt eines Bandes von aussen zur Mitte: Abstand = kw * w + kg * glow, Weissanteil, Deckkraft
struct Stop
{
	float kw, kg, tw, a;
};
// (Wunsch 27.11. 18:46 "leuchten nicht so schoen wie die Sensorringe ... mehr Farbe im Inneren und nicht den weissen Anteil"):
// der Koerper (normal gemischt) ist kraeftig farbig mit nur wenig Weiss in der Mitte; das Leuchten kommt als eigener Lichthof, der
// ADDITIV wie das Leuchten der Sensorringe Licht auf den Boden legt (weich auslaufend, volle Farbe, kein Grauschleier).
const Stop kProfile[] = {
	{ 1.f, 0.10f, 0.00f, 0.00f },  // weiche Kante (Kantenglaettung)
	{ 1.f, 0.f, 0.02f, 0.88f },    // Rand des Koerpers
	{ 0.62f, 0.f, 0.08f, 0.95f },  // Koerper
	{ 0.26f, 0.f, 0.18f, 0.98f },  // Kern
	{ 0.f, 0.f, 0.30f, 1.00f },    // Mitte (hell, aber farbig)
};
constexpr int NPROF = static_cast<int>(sizeof(kProfile) / sizeof(kProfile[0]));
const Stop kHalo[] = {
	{ 1.f, 1.f, 0.f, 0.00f },      // Ende des Lichthofs
	{ 1.f, 0.70f, 0.f, 0.10f },
	{ 1.f, 0.42f, 0.f, 0.26f },
	{ 1.f, 0.18f, 0.f, 0.46f },
	{ 1.f, 0.f, 0.f, 0.58f },      // an der Kante des Koerpers
	{ 0.f, 0.f, 0.10f, 0.30f },    // etwas Licht auch im Koerper
};
constexpr int NHALO = static_cast<int>(sizeof(kHalo) / sizeof(kHalo[0]));
/// Breite des Lichthofs aus der Leuchtbreite der Aufrufer (Sensorringe: weicher Schein von gut 20 Bildpunkten)
float haloWidth(float glow) { return glow * 2.3f + 4.f; }
} // namespace

void ribbon(const std::vector<glm::vec2> &ptsIn, float w, float glow, Colour c, float alpha, bool closed,
            const std::vector<float> &scaleIn, const std::vector<float> &alphaIn, bool roundCaps)
{
	if (ptsIn.size() < 2 || w <= 0.2f || alpha <= 0.f) { return; }
	std::vector<glm::vec2> pts = ptsIn;
	std::vector<float> sc = scaleIn, am = alphaIn;
	if (sc.size() != pts.size()) { sc.assign(pts.size(), 1.f); }
	if (am.size() != pts.size()) { am.assign(pts.size(), 1.f); }
	if (closed && pts.size() >= 3 && glm::length(pts.back() - pts.front()) < 1.5f)
	{
		pts.pop_back(); sc.pop_back(); am.pop_back();
	}
	if (closed && pts.size() < 3) { closed = false; }
	// weiche Enden: kurzes Stueck ueber das Ende hinaus, schmal und durchsichtig auslaufend
	if (!closed && roundCaps)
	{
		const auto cap = [&](bool atEnd) {
			const glm::vec2 e = atEnd ? pts.back() : pts.front();
			const glm::vec2 q = atEnd ? pts[pts.size() - 2] : pts[1];
			glm::vec2 d = e - q;
			const float l = glm::length(d);
			if (l < 1e-3f) { return; }
			d /= l;
			const float s0 = atEnd ? sc.back() : sc.front(), a0 = atEnd ? am.back() : am.front();
			const float ext = w * s0 * 0.9f;
			std::vector<glm::vec2> addP = { e + d * (ext * 0.5f), e + d * ext };
			std::vector<float> addS = { s0 * 0.78f, s0 * 0.35f }, addA = { a0 * 0.85f, a0 * 0.35f };
			if (atEnd)
			{
				pts.insert(pts.end(), addP.begin(), addP.end()); sc.insert(sc.end(), addS.begin(), addS.end()); am.insert(am.end(), addA.begin(), addA.end());
			}
			else
			{
				pts.insert(pts.begin(), addP.rbegin(), addP.rend()); sc.insert(sc.begin(), addS.rbegin(), addS.rend()); am.insert(am.begin(), addA.rbegin(), addA.rend());
			}
		};
		cap(true);
		cap(false);
	}
	const size_t n = pts.size();
	// Normalen (Mittel der Nachbarrichtungen)
	std::vector<glm::vec2> nrm(n, glm::vec2(0.f, 1.f));
	glm::vec2 lastN(0.f, 1.f);
	for (size_t i = 0; i < n; ++i)
	{
		const glm::vec2 prev = closed ? pts[(i + n - 1) % n] : pts[i > 0 ? i - 1 : 0];
		const glm::vec2 next = closed ? pts[(i + 1) % n] : pts[std::min(n - 1, i + 1)];
		glm::vec2 t = next - prev;
		const float l = glm::length(t);
		if (l > 1e-3f) { t /= l; lastN = glm::vec2(-t.y, t.x); }
		nrm[i] = lastN;
	}
	// Querschnitt: Abstaende von +aussen ueber die Mitte nach -aussen (2 * N - 1 Linien, dazwischen die Baender)
	const size_t m = closed ? n + 1 : n;
	const auto build = [&](const Stop *prof, int NP, float gw, Mesh &mesh) {
		const int K = 2 * NP - 1;
		std::vector<glm::vec2> la(m), lb(m);
		std::vector<RGBA> ca(m), cb(m);
		const auto line = [&](int k, std::vector<glm::vec2> &P, std::vector<RGBA> &C) {
			const int j = k < NP ? k : (K - 1 - k);   // Stufe im Profil
			const float sgn = k < NP - 1 ? 1.f : (k == NP - 1 ? 0.f : -1.f);
			const Stop &st = prof[j];
			for (size_t q = 0; q < m; ++q)
			{
				const size_t i = q % n;
				const float wi = w * sc[i];
				const float gi = gw * std::sqrt(std::clamp(sc[i], 0.25f, 1.f));
				P[q] = pts[i] + nrm[i] * (sgn * (st.kw * wi + st.kg * gi));
				C[q] = mix(c, st.tw, alpha * am[i] * st.a);
			}
		};
		line(0, la, ca);
		for (int k = 0; k + 1 < K; ++k)
		{
			line(k + 1, lb, cb);
			mesh.band(la, ca, lb, cb);
			la.swap(lb); ca.swap(cb);
		}
	};
	Mesh body, halo;
	build(kProfile, NPROF, glow, body);
	flush(body);
	build(kHalo, NHALO, haloWidth(glow), halo);
	flush(halo, true);
}

void arrowHead(const glm::vec2 &base, const glm::vec2 &tip, float halfW, float glow, Colour c, float alpha)
{
	glm::vec2 d = tip - base;
	const float L = glm::length(d);
	if (L < 1.f || halfW < 0.5f || alpha <= 0.f) { return; }
	d /= L;
	const glm::vec2 nl(-d.y, d.x);
	const glm::vec2 bl = base + nl * halfW, br = base - nl * halfW;
	Mesh mesh;
	// Lichthof an den beiden Flanken (nicht an der Basis - dort sitzt der Pfeilstamm): offener Zug br -> Spitze -> bl, Normale nach aussen
	const glm::vec2 e1 = tip - br, e2 = bl - tip;
	glm::vec2 n1(-e1.y, e1.x), n2(-e2.y, e2.x);
	n1 = glm::normalize(n1); n2 = glm::normalize(n2);
	const glm::vec2 ctr = (bl + br + tip) / 3.f;
	if (glm::dot(n1, (br + tip) * 0.5f - ctr) < 0.f) { n1 = -n1; }
	if (glm::dot(n2, (tip + bl) * 0.5f - ctr) < 0.f) { n2 = -n2; }
	glm::vec2 nt = n1 + n2;
	const float ntl = glm::length(nt);
	nt = ntl > 1e-3f ? nt / ntl : n1;
	const float miter = std::min(2.4f, 1.f / std::max(0.3f, glm::dot(nt, n1)));
	const std::vector<glm::vec2> path = { br, tip, bl };
	const std::vector<glm::vec2> pn = { n1, nt * miter, n2 };
	const float hg = haloWidth(glow);
	// Kante (weich, normal gemischt) und Lichthof (additiv) an den Flanken
	const float offs[2] = { glow * 0.10f, 0.f };
	const float alf[2] = { 0.f, 0.88f };
	const float hoffs[5] = { hg, hg * 0.70f, hg * 0.42f, hg * 0.18f, 0.f };
	const float half_[5] = { 0.f, 0.10f, 0.26f, 0.46f, 0.58f };
	Mesh glowMesh;
	for (int k = 0; k < 4; ++k)
	{
		std::vector<glm::vec2> a(3), b(3);
		std::vector<RGBA> ca(3), cb(3);
		for (int i = 0; i < 3; ++i)
		{
			a[i] = path[i] + pn[i] * hoffs[k];
			b[i] = path[i] + pn[i] * hoffs[k + 1];
			ca[i] = mix(c, 0.f, alpha * half_[k]);
			cb[i] = mix(c, 0.f, alpha * half_[k + 1]);
		}
		glowMesh.band(a, ca, b, cb);
	}
	{
		std::vector<glm::vec2> a(3), b(3);
		std::vector<RGBA> ca(3), cb(3);
		for (int i = 0; i < 3; ++i)
		{
			a[i] = path[i] + pn[i] * offs[0];
			b[i] = path[i] + pn[i] * offs[1];
			ca[i] = mix(c, 0.f, alpha * alf[0]);
			cb[i] = mix(c, 0.02f, alpha * alf[1]);
		}
		mesh.band(a, ca, b, cb);
	}
	// Flaeche: Faecher um einen hellen Punkt (Verlauf vom Rand zum Kern, wie der Querschnitt des Stammes - farbig, wenig Weiss)
	const glm::vec2 hot = base + d * (L * 0.34f);
	const RGBA edge = mix(c, 0.04f, alpha * 0.95f), core = mix(c, 0.30f, alpha);
	mesh.tri(br, tip, hot, edge, edge, core);
	mesh.tri(tip, bl, hot, edge, edge, core);
	mesh.tri(bl, br, hot, edge, edge, core);
	flush(mesh);
	flush(glowMesh, true);
}

void bars(const std::vector<std::pair<glm::vec2, glm::vec2>> &list, float halfW, Colour c, float alpha)
{
	if (list.empty() || halfW <= 0.2f || alpha <= 0.f) { return; }
	Mesh mesh, glowMesh;
	const float aa = 1.3f;
	const float offs[5] = { halfW + aa, halfW, 0.f, -halfW, -(halfW + aa) };
	const float tw[5] = { 0.f, 0.04f, 0.18f, 0.04f, 0.f };   // (27.11.) farbig statt weiss
	const float al[5] = { 0.f, 0.86f, 0.97f, 0.86f, 0.f };
	for (const auto &bar : list)
	{
		glm::vec2 d = bar.second - bar.first;
		const float l = glm::length(d);
		if (l < 0.5f) { continue; }
		d /= l;
		const glm::vec2 nl(-d.y, d.x);
		// Strich leicht verlaengert, Enden weich
		const glm::vec2 p0 = bar.first, p1 = bar.second + d * (halfW * 0.4f);
		for (int k = 0; k < 4; ++k)
		{
			const std::vector<glm::vec2> a = { p0 + nl * offs[k], p1 + nl * offs[k] };
			const std::vector<glm::vec2> b = { p0 + nl * offs[k + 1], p1 + nl * offs[k + 1] };
			const std::vector<RGBA> ca = { mix(c, tw[k], alpha * al[k]), mix(c, tw[k], alpha * al[k] * 0.55f) };
			const std::vector<RGBA> cb = { mix(c, tw[k + 1], alpha * al[k + 1]), mix(c, tw[k + 1], alpha * al[k + 1] * 0.55f) };
			mesh.band(a, ca, b, cb);
		}
		// (27.11.) Lichthof additiv wie bei den Baendern
		const float hg = std::max(3.f, halfW * 2.2f);
		const float ho[7] = { halfW + hg, halfW + hg * 0.45f, halfW, 0.f, -halfW, -(halfW + hg * 0.45f), -(halfW + hg) };
		const float ha[7] = { 0.f, 0.22f, 0.5f, 0.32f, 0.5f, 0.22f, 0.f };
		for (int k = 0; k < 6; ++k)
		{
			const std::vector<glm::vec2> a = { p0 + nl * ho[k], p1 + nl * ho[k] };
			const std::vector<glm::vec2> b = { p0 + nl * ho[k + 1], p1 + nl * ho[k + 1] };
			const std::vector<RGBA> ca = { mix(c, 0.f, alpha * ha[k]), mix(c, 0.f, alpha * ha[k] * 0.6f) };
			const std::vector<RGBA> cb = { mix(c, 0.f, alpha * ha[k + 1]), mix(c, 0.f, alpha * ha[k + 1] * 0.6f) };
			glowMesh.band(a, ca, b, cb);
		}
	}
	flush(mesh);
	flush(glowMesh, true);
}

void fan(const glm::vec2 &center, const std::vector<glm::vec2> &ring, Colour c, float alpha, float centerWhite)
{
	if (ring.size() < 3 || alpha <= 0.f) { return; }
	Mesh mesh;
	const RGBA edge = mix(c, 0.f, alpha), mid = mix(c, centerWhite, alpha);
	for (size_t i = 0; i < ring.size(); ++i) { mesh.tri(center, ring[i], ring[(i + 1) % ring.size()], mid, edge, edge); }
	flush(mesh);
}

void shutdown()
{
	s_pool.clear();
	s_used = 0;
	s_frame = static_cast<size_t>(-1);
}
}
