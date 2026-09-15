#include "ThermalVisionSceneViewExtension.h"
#include "HAL/IConsoleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"

static TAutoConsoleVariable<bool> CVarThermalVisionEnable(
	TEXT("r.ThermalVision.Enable"), false,
	TEXT("Enables the thermal vision post-process."));


bool FThermalVisionSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarThermalVisionEnable.GetValueOnGameThread();
}

void FThermalVisionSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FThermalVisionSceneViewExtension::PostProcessPassAfterTonemap_RenderThread));
	}
}

FScreenPassTexture FThermalVisionSceneViewExtension::PostProcessPassAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
}