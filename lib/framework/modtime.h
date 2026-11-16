// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (16.11.): Ladezeiten messen - schreibt "MOD-Ladezeit: <Name>: <ms> ms" ins Log, wenn ein Abschnitt laenger als minMs dauert.
#pragma once

#include <chrono>
#include <string>

#include "frame.h"

struct ModLoadTimer
{
	std::string name;
	int minMs;
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	explicit ModLoadTimer(std::string n, int minMs_ = 100) : name(std::move(n)), minMs(minMs_) {}
	~ModLoadTimer()
	{
		const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
		if (ms >= minMs)
		{
			debug(LOG_INFO, "MOD-Ladezeit: %s: %d ms", name.c_str(), ms);
		}
	}
};
