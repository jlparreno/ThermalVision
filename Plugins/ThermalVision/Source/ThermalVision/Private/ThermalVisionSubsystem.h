#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "ThermalVisionSubsystem.generated.h"

class FThermalVisionSceneViewExtension;

/**
 * Thermal Vision Subsystem
 */
UCLASS()
class UThermalVisionSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:

	TSharedPtr<FThermalVisionSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;
};
