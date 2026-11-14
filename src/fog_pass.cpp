// SPDX-License-Identifier: GPL-2.0-or-later

/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project (https://github.com/Warzone2100)

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file fog_pass.cpp
 * Deferred distance-fog fullscreen pass.
 */

#include "fog_pass.h"

#include "display3d.h"
#include "display3d_render_graph.h"
#include "display3d_render_internal.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/piestate.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>

namespace fog_pass
{

void recordApply(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 2, "FogApply: 0 color, 1 prepass depth");
	gfx_api::abstract_texture* scene = passCtx.getRead(0);
	gfx_api::abstract_texture* depth = passCtx.getRead(1);
	if (scene == nullptr || depth == nullptr
		|| !pie_IsInGame3DFrameContextReady())
	{
		return;
	}

	const RENDER_STATE renderState = getCurrentRenderState();
	const auto& fc = pie_GetInGame3DFrameContext();

	gfx_api::constant_buffer_type<SHADER_SCENE_FOG> constants {};
	constants.fogColor = pie_SceneColourToVec4(pie_GetFogColour());
	constants.invProjectionMatrix = glm::inverse(fc.perspectiveMatrix);
	display3d_fillPassReadUvScaleClamp(passCtx, 1, constants.uvScaleClamp);
	constants.fogBegin = renderState.fogBegin;
	constants.fogEnd = renderState.fogEnd;
	constants.linearLighting = gfx_api::context::get().linearLightingEnabled() ? 1.f : 0.f;

	// MOD: Schaerfentiefe (reine Bildschirmdarstellung, hat mit dem Spielablauf nichts zu tun):
	// Fokus = Kameraabstand zum Blickpunkt; scharf von 55 % bis 160 % davon, dahinter/davor weicher Uebergang bis zum groessten Radius.
	{
		const float focus = std::max(getViewDistance(), 300.f);
		const auto dims = gfx_api::context::get().getRenderTargetDimensions(scene)
			.value_or(std::pair<uint32_t, uint32_t>{1, 1});
		const float w = static_cast<float>(std::max<uint32_t>(dims.first, 1));
		const float h = static_cast<float>(std::max<uint32_t>(dims.second, 1));
		const float viewportHeight = h * constants.uvScaleClamp.y;
		constants.dofParams = glm::vec4(focus, focus * 0.55f, focus * 1.6f, focus * 1.2f);
		constants.dofTexel = glm::vec4(1.f / w, 1.f / h, 2.6f * viewportHeight / 1080.f, 1.f);
	}
	display3d_drawFullscreenTriangle<gfx_api::SceneFogPSO>(constants, scene, depth);
}

} // namespace fog_pass
