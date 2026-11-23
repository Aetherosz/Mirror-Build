#pragma once

#include "CoreMinimal.h"

namespace MirrorLabels
{
	/** Before any building is drawn: gives the meshes with writing a material that keeps it readable when mirrored. */
	void Register();

	/** Changes the loaded assets only, never the files. Returns how many mesh slots changed. */
	int32 Apply();
}
