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
	TEXT("r.ThermalVision.TemperatureMin"), 5.0f,
	TEXT("Temperature in Celsius mapped to the cold end of the palette. Tuned to the interior scene: a narrow range keeps the 5 to 50 band readable and lets lamps and hot cables clip to white, the way a real camera auto ranges."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionTemperatureMax(
	TEXT("r.ThermalVision.TemperatureMax"), 50.0f,
	TEXT("Temperature in Celsius mapped to the hot end of the palette."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionLuminanceTemperatureGain(
	TEXT("r.ThermalVision.LuminanceTemperatureGain"), 30.0f,
	TEXT("Degrees Celsius added to the ambient temperature at full scene luminance."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionSkyTemperature(
	TEXT("r.ThermalVision.SkyTemperature"), -20.0f,
	TEXT("Temperature in Celsius used for pixels with no geometry."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarThermalVisionBlurRadius(
	TEXT("r.ThermalVision.BlurRadius"), 3,
	TEXT("Radius in pixels of the separable blur applied to the temperature field. 0 disables both blur passes."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionBlurSigma(
	TEXT("r.ThermalVision.BlurSigma"), 4.0f,
	TEXT("Standard deviation in pixels of the Gaussian weights used by the blur."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionNoiseTemporal(
	TEXT("r.ThermalVision.NoiseTemporal"), 4.0f,
	TEXT("Peak to peak amplitude in Celsius of the per frame sensor grain. 0 disables it."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThermalVisionNoiseFixedPattern(
	TEXT("r.ThermalVision.NoiseFixedPattern"), 4.0f,
	TEXT("Peak to peak amplitude in Celsius of the static per pixel and per column pattern noise. 0 disables it."),
	ECVF_RenderThreadSafe);

// Reads scene color, custom stencil and scene depth, and writes raw Celsius into a texture of its own.
static FRDGTextureRef AddThermalVisionTemperaturePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSceneTextureShaderParameters& SceneTextures, const FScreenPassTextureViewport& InputViewport, FRDGTextureRef SceneColorTexture, const FRDGTextureDesc& TemperatureDesc,	float AmbientTemperature, float LuminanceTemperatureGain, float SkyTemperature)
{
	FRDGTextureRef TemperatureTexture = GraphBuilder.CreateTexture(TemperatureDesc, TEXT("ThermalVision.Temperature"));

	FThermalVisionTemperatureCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThermalVisionTemperatureCS::FParameters>();
	PassParameters->InputTexture = SceneColorTexture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->SceneTextures = SceneTextures;

	// Input rect in UV space, used by the shader to go from input UVs to viewport UVs.
	PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);

	PassParameters->AmbientTemperature = AmbientTemperature;
	PassParameters->LuminanceTemperatureGain = LuminanceTemperatureGain;
	PassParameters->SkyTemperature = SkyTemperature;

	// Texture to write on
	PassParameters->OutTemperatureTexture = GraphBuilder.CreateUAV(TemperatureTexture);

	TShaderMapRef<FThermalVisionTemperatureCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThermalVision.Temperature %dx%d", InputViewport.Rect.Width(), InputViewport.Rect.Height()),
		ERDGPassFlags::Compute,
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(InputViewport.Rect.Size(), FThermalVisionTemperatureCS::ThreadGroupSize));

	return TemperatureTexture;
}

// One direction of the separable blur. Both passes run the same shader: only the direction changes.
static void AddThermalVisionBlurPass(FRDGBuilder& GraphBuilder,	const FSceneView& View,	const FScreenPassTextureViewport& InputViewport, FRDGTextureRef TemperatureTexture,	FRDGTextureRef OutTemperatureTexture, FIntPoint BlurDirection, int32 BlurRadius, float BlurSigma)
{
	FThermalVisionBlurCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThermalVisionBlurCS::FParameters>();
	PassParameters->TemperatureTexture = TemperatureTexture;
	PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->BlurDirection = BlurDirection;
	PassParameters->BlurRadius = BlurRadius;
	PassParameters->BlurSigma = BlurSigma;
	PassParameters->OutTemperatureTexture = GraphBuilder.CreateUAV(OutTemperatureTexture);

	TShaderMapRef<FThermalVisionBlurCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThermalVision.Blur %s", BlurDirection.X != 0 ? TEXT("Horizontal") : TEXT("Vertical")),
		ERDGPassFlags::Compute,
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(InputViewport.Rect.Size(), FThermalVisionBlurCS::ThreadGroupSize));
}

// Horizontal then vertical blur of the temperature field, each on its own texture.
// Returns the input texture untouched when the radius is 0: then no pass is added to the graph at all.
static FRDGTextureRef AddThermalVisionBlurPasses(FRDGBuilder& GraphBuilder,	const FSceneView& View,	const FScreenPassTextureViewport& InputViewport, FRDGTextureRef TemperatureTexture,	const FRDGTextureDesc& TemperatureDesc, int32 BlurRadius, float BlurSigma)
{
	if (BlurRadius <= 0)
	{
		return TemperatureTexture;
	}

	FRDGTextureRef HorizontalTexture = GraphBuilder.CreateTexture(TemperatureDesc, TEXT("ThermalVision.TemperatureBlurH"));
	FRDGTextureRef VerticalTexture = GraphBuilder.CreateTexture(TemperatureDesc, TEXT("ThermalVision.TemperatureBlurHV"));

	AddThermalVisionBlurPass(GraphBuilder, View, InputViewport, TemperatureTexture, HorizontalTexture, FIntPoint(1, 0), BlurRadius, BlurSigma);
	AddThermalVisionBlurPass(GraphBuilder, View, InputViewport, HorizontalTexture, VerticalTexture, FIntPoint(0, 1), BlurRadius, BlurSigma);

	return VerticalTexture;
}

// Normalizes the temperature, applies the palette and writes the pass output. Raster, because
// OverrideOutput is not guaranteed to support UAV writes.
static void AddThermalVisionCompositePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassTextureViewport& InputViewport, const FScreenPassTextureViewport& OutputViewport, FRDGTextureRef SceneColorTexture, FRDGTextureRef TemperatureTexture, const FScreenPassRenderTarget& Output, float TemperatureRangeMin, float TemperatureRangeMax, float NoiseTemporalAmount, float NoiseFixedPatternAmount)
{
	// Allocated in the graph builder memory because the pass executes after this function returns.
	FThermalVisionCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThermalVisionCompositePS::FParameters>();
	PassParameters->InputTexture = SceneColorTexture;
	PassParameters->TemperatureTexture = TemperatureTexture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Input rect in UV space, used by the shader to go from input UVs to viewport UVs.
	PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);

	PassParameters->TemperatureRangeMin = TemperatureRangeMin;
	PassParameters->TemperatureRangeMax = TemperatureRangeMax;

	// The view uniform buffer the renderer already uploaded: the shader only needs its per frame counter.
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->NoiseTemporalAmount = NoiseTemporalAmount;
	PassParameters->NoiseFixedPatternAmount = NoiseFixedPatternAmount;

	// Compiled at startup into the global shader map for this feature level.
	TShaderMapRef<FThermalVisionCompositePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThermalVision.Composite %dx%d", Output.ViewRect.Width(), Output.ViewRect.Height()),
		View,
		OutputViewport,
		InputViewport,
		PixelShader,
		PassParameters);
}

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

	// Intermediate temperature textures: one channel, 16 bit float, because Celsius needs range, not precision. No render target flag: only compute writes them.
	const FRDGTextureDesc TemperatureDesc = FRDGTextureDesc::Create2D(
		InputViewport.Extent,
		PF_R16F,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	// Render thread reads: the cvars are ECVF_RenderThreadSafe, so these are the values for this frame.
	const int32 BlurRadius = FMath::Clamp(CVarThermalVisionBlurRadius.GetValueOnRenderThread(), 0, 8);
	const float BlurSigma = FMath::Clamp(CVarThermalVisionBlurSigma.GetValueOnRenderThread(), 0.01f, 8.0f);

	// Turns the tonemapped image and the stencil values into one temperature in Celsius per pixel.
	FRDGTextureRef TemperatureTexture = AddThermalVisionTemperaturePass(
		GraphBuilder,
		View,
		Inputs.SceneTextures,
		InputViewport,
		SceneColor.Texture,
		TemperatureDesc,
		CVarThermalVisionAmbientTemperature.GetValueOnRenderThread(),
		CVarThermalVisionLuminanceTemperatureGain.GetValueOnRenderThread(),
		CVarThermalVisionSkyTemperature.GetValueOnRenderThread());

	// Two blur passes over the temperature field: a real thermal camera blurs in the optics, before the color mapping.
	TemperatureTexture = AddThermalVisionBlurPasses(
		GraphBuilder,
		View,
		InputViewport,
		TemperatureTexture,
		TemperatureDesc,
		BlurRadius,
		BlurSigma);

	// Normalizes the temperature into the configured range and applies the palette.
	AddThermalVisionCompositePass(
		GraphBuilder,
		View,
		InputViewport,
		OutputViewport,
		SceneColor.Texture,
		TemperatureTexture,
		Output,
		CVarThermalVisionTemperatureMin.GetValueOnRenderThread(),
		CVarThermalVisionTemperatureMax.GetValueOnRenderThread(),
		FMath::Max(CVarThermalVisionNoiseTemporal.GetValueOnRenderThread(), 0.0f),
		FMath::Max(CVarThermalVisionNoiseFixedPattern.GetValueOnRenderThread(), 0.0f));

	// The next post process pass reads this texture. If we were the last pass, it's already the final target.
	return MoveTemp(Output);
}