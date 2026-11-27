#pragma once

#include "CoreMinimal.h"

/**
 * Keeps the mirror through a blueprint.
 *
 * The game's blueprint path doesn't keep the negative scale, so a mirrored building came out plain but
 * still carrying its half turn: back to front. When a blueprint is saved, the mirrored buildings in it
 * are written to a small file next to it (<name>.sbpmirror). When it's loaded (hologram preview,
 * placement, designer), each building found there at the same spot is mirrored again before it starts.
 */
namespace MirrorBlueprints
{
	void Register();
}
