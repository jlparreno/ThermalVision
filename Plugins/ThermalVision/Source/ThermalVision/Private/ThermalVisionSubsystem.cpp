#include "ThermalVisionSubsystem.h"
#include "ThermalVisionSceneViewExtension.h"
#include "ThermalVisionLog.h"


void UThermalVisionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SceneViewExtension = FSceneViewExtensions::NewExtension<FThermalVisionSceneViewExtension>();

	UE_LOG(LogThermalVision, Log, TEXT("ThermalVision Subsystem Initialized"));
}

void UThermalVisionSubsystem::Deinitialize()
{
	SceneViewExtension.Reset();

	UE_LOG(LogThermalVision, Log, TEXT("ThermalVision Subsystem Deinitialized"));

	Super::Deinitialize();
}