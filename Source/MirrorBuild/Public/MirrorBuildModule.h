#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMirrorBuildModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
