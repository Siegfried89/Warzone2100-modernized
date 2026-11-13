// SPDX-License-Identifier: GPL-2.0-or-later
// ERZEUGT von w/voice/emit_table.py - nicht von Hand aendern.
#pragma once
struct VoiceCat { const char *name; int lines; int voices; };
static const VoiceCat kVoiceCats[] = {
	{"inf_cover", 5, 4},
	{"inf_hit", 5, 4},
	{"inf_help", 4, 4},
	{"inf_pinned", 3, 4},
	{"inf_tanks", 5, 4},
	{"inf_retreat", 5, 4},
	{"inf_enemy", 5, 4},
	{"inf_ack", 6, 4},
	{"inf_attack", 5, 4},
	{"inf_kill", 3, 4},
	{"inf_ammo", 3, 4},
	{"tnk_advance", 7, 3},
	{"tnk_contact", 6, 3},
	{"tnk_hit", 5, 3},
	{"tnk_retreat", 5, 3},
	{"tnk_ack", 7, 3},
	{"tnk_kill", 4, 3},
	{"tnk_ammo", 3, 3},
	{"trn_fire", 5, 2},
	{"trn_drop", 3, 2},
	{"arty_inf", 4, 4},
	{"arty_tnk", 4, 3},
	{"arty_trn", 2, 2},
	{"help_call", 2, 3},
};
