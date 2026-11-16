// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (16.11.): schnelleres Laden ohne Funktionsverlust - einfache Parallel-Schleife fuer reine Rechenarbeit
// (PNG entpacken, Mipmaps, Echtzeit-Kompression). Keine Grafik-Aufrufe in fn! Hochladen bleibt im Hauptfaden.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace wzparallel
{
/// Anzahl Arbeiter (inkl. aufrufendem Faden): Kerne, hoechstens 8
inline unsigned workerCount()
{
	const unsigned hc = std::thread::hardware_concurrency();
	return std::max(1u, std::min(hc == 0 ? 2u : hc, 8u));
}

/// fn(i) fuer i in [begin, end) - verteilt auf mehrere Faeden, der aufrufende Faden arbeitet mit.
/// Reihenfolge der Ausfuehrung ist beliebig; fn muss threadsicher sein und darf keine Ausnahmen werfen.
template <typename Fn>
void forRange(size_t begin, size_t end, Fn &&fn)
{
	if (end <= begin) { return; }
	const size_t n = end - begin;
	const unsigned workers = static_cast<unsigned>(std::min<size_t>(workerCount(), n));
	if (workers <= 1)
	{
		for (size_t i = begin; i < end; ++i) { fn(i); }
		return;
	}
	std::atomic<size_t> next{begin};
	auto run = [&]() {
		for (;;)
		{
			const size_t i = next.fetch_add(1);
			if (i >= end) { break; }
			fn(i);
		}
	};
	std::vector<std::thread> threads;
	threads.reserve(workers - 1);
	for (unsigned t = 1; t < workers; ++t)
	{
		threads.emplace_back(run);
	}
	run();
	for (auto &th : threads) { th.join(); }
}
}
