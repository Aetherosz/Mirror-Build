#include "MirrorBuildModule.h"

#include "MirrorHooks.h"
#include "MirrorInput.h"
#include "MirrorInstances.h"
#include "MirrorLabels.h"

void FMirrorBuildModule::StartupModule()
{
	// The editor only has header stubs of the game, so there is nothing to hook there.
#if !WITH_EDITOR
	FMirrorHooks::Register();
	FMirrorInput::Register();
	MirrorLabels::Register();
	MirrorInstances::Register();
#endif
}

IMPLEMENT_MODULE(FMirrorBuildModule, MirrorBuild)
