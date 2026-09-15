#include "ThermalVisionSceneViewExtension.h"
#include "HAL/IConsoleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ThermalVisionShaders.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"

DECLARE_GPU_STAT_NAMED(ThermalVision, TEXT("ThermalVision"));

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
	RDG_EVENT_SCOPE_STAT(GraphBuilder, ThermalVision, "ThermalVision");

	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	check(SceneColor.IsValid());

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	// If the override output is provided, it means that this is the last pass in post processing.
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThermalVision.Output"));
	}

	// Shader input parameters
	FThermalVisionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThermalVisionPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Get global shader
	TShaderMapRef<FThermalVisionPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	// Add pass
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThermalVision %dx%d", Output.ViewRect.Width(), Output.ViewRect.Height()),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}