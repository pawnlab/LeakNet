// Get rid of:
//	warning C4049: compiler limit : terminating line number emission
#ifdef _WIN32
#pragma warning (disable:4049)
#endif
#define DEFINE_SHADERS
#include "tier0/dbg.h"
#include "materialsystem/ishader.h"
#include "shaderlib/shaderDLL.h"
#include "fxctmp9\Bloom_ps20.inc"
#include "fxctmp9\BlurFilter_ps20.inc"
#include "fxctmp9\BlurFilter_vs20.inc"
#include "fxctmp9\Downsample_ps20.inc"
#include "fxctmp9\Downsample_vs20.inc"
#include "fxctmp9\Refract_ps20.inc"
#include "fxctmp9\Refract_vs20.inc"
#include "fxctmp9\ShatteredGlass_ps20.inc"
#include "fxctmp9\ShatteredGlass_vs20.inc"
#include "fxctmp9\ShowDestAlpha_ps20.inc"
#include "fxctmp9\WaterCheap_ps20.inc"
#include "fxctmp9\WaterCheap_vs20.inc"
#include "fxctmp9\Water_ps20.inc"
#include "fxctmp9\Water_vs20.inc"
#include "pshtmp9\WorldVertexAlpha.inc" // VXP
#include "vshtmp9\WorldVertexAlpha.inc" // VXP
#include "fxctmp9\WorldVertexAlpha_ps20.inc"
#include "fxctmp9\WorldVertexTransition_ps20.inc"
#include "fxctmp9\WorldVertexTransition_vs20.inc"
#include "fxctmp9\hsv_ps20.inc"
#include "fxctmp9\lightmappedgeneric_hdr_ps20.inc"
#include "fxctmp9\lightmappedgeneric_hdr_vs20.inc"
#include "fxctmp9\screenspaceeffect_vs20.inc"
#include "fxctmp9\shadow_ps20.inc"
#include "fxctmp9\shadow_vs20.inc"
#include "fxctmp9\sky_ps20.inc"
#include "fxctmp9\sky_vs20.inc"
#include "fxctmp9\teeth_vs20.inc"
#include "fxctmp9\vertexlit_and_unlit_generic_hdr_ps20.inc"
#include "fxctmp9\vertexlit_and_unlit_generic_hdr_vs20.inc"
