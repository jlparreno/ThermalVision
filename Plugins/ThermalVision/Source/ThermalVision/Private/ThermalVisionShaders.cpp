#include "ThermalVisionShaders.h"

IMPLEMENT_GLOBAL_SHADER(FThermalVisionTemperatureCS, "/Plugin/ThermalVision/Private/ThermalVision.usf", "TemperatureCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThermalVisionBlurCS, "/Plugin/ThermalVision/Private/ThermalVision.usf", "BlurCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThermalVisionCompositePS, "/Plugin/ThermalVision/Private/ThermalVision.usf", "CompositePS", SF_Pixel);