#include "ThermalVision.h"

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FThermalVisionModule"

void FThermalVisionModule::StartupModule()
{
	const FString ShaderPath = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ThermalVision"))->GetBaseDir(), TEXT("Shaders"));

	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ThermalVision"), ShaderPath);
}

void FThermalVisionModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FThermalVisionModule, ThermalVision)