#pragma once

#include "CoreMinimal.h"

/**
 * What can be mirrored: production, power, storage and station buildings with a belt or pipe input or
 * output, vanilla or modded. Logistics pieces, belts, pipes, poles, walls and decor can't.
 */
struct FMirrorRule
{
	bool bCanMirror = false;

	/**
	 * The mirror itself swaps the Y ends. Buildings with their ports on those ends get a half turn with
	 * it, so it becomes a left/right mirror. Buildings with ports on an X end don't need one.
	 */
	bool bHalfTurn = false;

	int32 NumPorts = 0;
};

namespace MirrorRules
{
	/** Worked out once per class, from the class alone, so client and server always agree. */
	FMirrorRule Get(const UClass* BuildClass);
}
