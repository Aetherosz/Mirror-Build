#pragma once

#include "CoreMinimal.h"

namespace MirrorInstances
{
	/** Mirrored buildings draw their lightweight instances as plain meshes, so they aren't lit inside-out. */
	void Register();
}
