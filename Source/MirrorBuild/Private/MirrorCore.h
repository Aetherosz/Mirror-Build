#pragma once

#include "CoreMinimal.h"

class AActor;
class AFGHologram;
class IFGConstructionMessageInterface;

DECLARE_LOG_CATEGORY_EXTERN(LogMirrorBuild, Log, All);

/**
 * A mirrored building is the normal actor with a negative Y scale on its root. Nothing else is stored.
 *
 * It has to be Y. The connection normal is the X axis of the component's rotation, and with a negative
 * X scale the engine turns that axis round, so every port would point into the building.
 *
 * A Y mirror alone swaps the Y ends, so for buildings with ports there toggling also turns it 180 degrees
 * (see FMirrorRule). Either way inputs and outputs stay on their side of the building.
 */
namespace MirrorCore
{
	/** See MirrorRules.h for what qualifies. */
	bool CanMirror(const UClass* BuildClass);

	bool IsMirrored(const AActor* Actor);

	/** Sets the sign of the root's Y scale and keeps its size. */
	void SetActorMirrored(AActor* Actor, bool bMirrored);

	/** Same, plus what only a hologram needs, plus children of the same class (grid copies). */
	void SetHologramMirrored(AFGHologram* Hologram, bool bMirrored);

	/** The key press: mirror, and the half turn where the building needs one. */
	void Toggle(AFGHologram* Hologram);

	/**
	 * One bit at the end of the construction message.
	 * Takes the interface pointer on purpose: that is what the hook receives, not the actor's address.
	 */
	void SerializeMirrorFlag(IFGConstructionMessageInterface* Message, FArchive& Ar);
}
