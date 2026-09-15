#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "ScreenPass.h"


class FThermalVisionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThermalVisionPS);

	SHADER_USE_PARAMETER_STRUCT(FThermalVisionPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Tonemapped SceneColor from the previous pass (output resolution). Declared as RDG texture so RDG tracks it as read by this pass.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		// Sampler for InputTexture. Point clamp: input and output rects have the same size, so each pixel reads its own texel.
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		// Reference to the view uniform buffer the renderer already uploaded, to access the render resolution rect and depth linearization.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		// Scene textures RDG uniform buffer, used to read scene depth. RDG marks every texture inside it as read by this pass.
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		// Rect of the SceneColor input inside its texture, to convert input UVs to viewport UVs. Must match SCREEN_PASS_TEXTURE_VIEWPORT(Input) in the .usf.
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		// Relative depth difference to the farthest neighbor above which a pixel is an outline. Read from r.ThermalVision.OutlineThreshold on the render thread.
		SHADER_PARAMETER(float, OutlineDepthThreshold)
		// Render targets written by this pass. RenderTargets[0] is the pass output (OverrideOutput or a new texture).
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
