#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "ScreenPass.h"
#include "RenderGraphUtils.h"

class FThermalVisionTemperatureCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThermalVisionTemperatureCS);

	SHADER_USE_PARAMETER_STRUCT(FThermalVisionTemperatureCS, FGlobalShader);

	// Matches the [numthreads] in the shader and the group count of the dispatch.
	static constexpr int32 ThreadGroupSize = FComputeShaderUtils::kGolden2DGroupSize;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Tonemapped SceneColor from the previous pass (output resolution). Declared as RDG texture so RDG tracks it as read by this pass.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		// Sampler for InputTexture. Point clamp: the pass runs at the input resolution, so each thread reads its own texel.
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		// Reference to the view uniform buffer the renderer already uploaded, to access the render resolution rect
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		// Scene textures RDG uniform buffer, used to read scene depth, custom depth and custom stencil. RDG marks every texture inside it as read by this pass.
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		// Rect of the SceneColor input inside its texture, to convert input UVs to viewport UVs. Must match SCREEN_PASS_TEXTURE_VIEWPORT(Input) in the .usf.
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		// Temperature in Celsius for pixels with no custom stencil value. Placeholder kept as the base of the ambient estimation.
		SHADER_PARAMETER(float, AmbientTemperature)
		// Degrees Celsius added to the ambient temperature at full scene luminance. Fakes sunlit or lit surfaces reading warmer.
		SHADER_PARAMETER(float, LuminanceTemperatureGain)
		// Temperature in Celsius for pixels with no geometry: the sky reads cold on a real thermal camera.
		SHADER_PARAMETER(float, SkyTemperature)
		// Temperature in Celsius, one channel. No render target: compute writes through a UAV.
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutTemperatureTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize); // Set this to ensure same value in C++ and HLSL
	}
};

class FThermalVisionBlurCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThermalVisionBlurCS);

	SHADER_USE_PARAMETER_STRUCT(FThermalVisionBlurCS, FGlobalShader);

	// Matches the [numthreads] in the shader and the group count of the dispatch.
	static constexpr int32 ThreadGroupSize = FComputeShaderUtils::kGolden2DGroupSize;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Temperature field to filter: the temperature pass output for the horizontal pass, its result for the vertical one.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TemperatureTexture)
		// View rect inside the temperature texture: bounds the dispatch and clamps the taps. Must match SCREEN_PASS_TEXTURE_VIEWPORT(Input) in the .usf.
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		// (1,0) for the horizontal pass, (0,1) for the vertical one.
		SHADER_PARAMETER(FIntPoint, BlurDirection)
		// Number of taps to each side of the center. 0 means the blur passes are not added to the graph at all.
		SHADER_PARAMETER(int32, BlurRadius)
		// Standard deviation of the Gaussian weights, in pixels. Independent of the radius because the weights are normalized.
		SHADER_PARAMETER(float, BlurSigma)
		// Blurred output, one channel. No render target: compute writes through a UAV.
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutTemperatureTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize); // Set this to ensure same value in C++ and HLSL
	}
};

class FThermalVisionCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThermalVisionCompositePS);

	SHADER_USE_PARAMETER_STRUCT(FThermalVisionCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Tonemapped SceneColor from the previous pass (output resolution). Declared as RDG texture so RDG tracks it as read by this pass.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		// Blurred temperature field in Celsius, produced by the compute passes.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TemperatureTexture)
		// Sampler for InputTexture. Point clamp: input and output rects have the same size, so each pixel reads its own texel.
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		// Rect of the SceneColor input inside its texture, to convert input UVs to viewport UVs. Must match SCREEN_PASS_TEXTURE_VIEWPORT(Input) in the .usf.
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		// Temperature in Celsius mapped to the cold end of the palette. Everything below saturates.
		SHADER_PARAMETER(float, TemperatureRangeMin)
		// Temperature in Celsius mapped to the hot end of the palette. Everything above saturates.
		SHADER_PARAMETER(float, TemperatureRangeMax)
		// Render targets written by this pass. RenderTargets[0] is the pass output (OverrideOutput or a new texture).
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
