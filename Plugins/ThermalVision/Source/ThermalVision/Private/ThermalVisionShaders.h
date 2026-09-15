#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"


class FThermalVisionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThermalVisionPS);

	SHADER_USE_PARAMETER_STRUCT(FThermalVisionPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
