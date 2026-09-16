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

static TAutoConsoleVariable<float> CVarThermalVisionAmbientTemperature(
	TEXT("r.ThermalVision.AmbientTemperature"), 20.0f,
	TEXT("Temperature in Celsius used for pixels with no custom stencil value."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionTemperatureMin(
	TEXT("r.ThermalVision.TemperatureMin"), 0.0f,
	TEXT("Temperature in Celsius mapped to the cold end of the palette."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionTemperatureMax(
	TEXT("r.ThermalVision.TemperatureMax"), 100.0f,
	TEXT("Temperature in Celsius mapped to the hot end of the palette."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionLuminanceTemperatureGain(
	TEXT("r.ThermalVision.LuminanceTemperatureGain"), 15.0f,
	TEXT("Degrees Celsius added to the ambient temperature at full scene luminance."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionSkyTemperature(
	TEXT("r.ThermalVision.SkyTemperature"), -20.0f,
	TEXT("Temperature in Celsius used for pixels with no geometry."),
	ECVF_RenderThreadSafe);


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
	// Runs on the render thread while the graph is being built: this only declares passes,
	// the GPU work happens later in GraphBuilder.Execute().

	// Groups every pass below under one name in RenderDoc / ProfileGPU and attaches the GPU stat.
	RDG_EVENT_SCOPE_STAT(GraphBuilder, ThermalVision, "ThermalVision");

	// Inputs are texture slices. For a plain 2D texture this returns the same texture and view rect
	// without copying; it only copies when the input is a slice of a texture array.
	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	check(SceneColor.IsValid());

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	// If the override output is provided, it means that this is the last pass in post processing.
	// Otherwise create a new texture with the same description and view rect as the input: a pass can't
	// read and write the same texture.
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThermalVision.Output"));
	}

	// Texture extent + view rect of each side. The pooled textures are usually larger than the view,
	// so UVs must be derived from these instead of assuming a fullscreen texture.
	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	// Shader input parameters
	// Allocated in the graph builder memory because the pass executes after this function returns.
	FThermalVisionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThermalVisionPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Scene depth lives at render resolution: the View uniform buffer describes that rect and the scene
	// textures buffer holds the depth. Same bindings the engine uses for post process materials.
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->SceneTextures = Inputs.SceneTextures;
	
	// Input rect in UV space, used by the shader to go from input UVs to viewport UVs.
	PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);

	// Render thread read: the cvars are ECVF_RenderThreadSafe, so this is the value for this frame.
	PassParameters->AmbientTemperature = CVarThermalVisionAmbientTemperature.GetValueOnRenderThread();
	PassParameters->TemperatureRangeMin = CVarThermalVisionTemperatureMin.GetValueOnRenderThread();
	PassParameters->TemperatureRangeMax = CVarThermalVisionTemperatureMax.GetValueOnRenderThread();
	PassParameters->LuminanceTemperatureGain = CVarThermalVisionLuminanceTemperatureGain.GetValueOnRenderThread();
	PassParameters->SkyTemperature = CVarThermalVisionSkyTemperature.GetValueOnRenderThread();

	// Get global shader
	// Compiled at startup into the global shader map for this feature level.
	TShaderMapRef<FThermalVisionPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	// Add pass
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThermalVision %dx%d", Output.ViewRect.Width(), Output.ViewRect.Height()),
		View,
		OutputViewport,
		InputViewport,
		PixelShader,
		PassParameters);

	// The next post process pass reads this texture. If we were the last pass, it's already the final target.
	return MoveTemp(Output);
}