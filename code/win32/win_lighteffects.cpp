#include "../server/exe_headers.h"

#include "win_lighteffects.h"

LightEffects::LightEffects()
{
}

LightEffects::~LightEffects()
{
}

bool LightEffects::Initialize()
{
    return false;
}

void LightEffects::ProcessVertices(void* pDirLightDir, void* pPtLightPos)
{
}

bool LightEffects::RenderDynamicLights()
{
    return false;
}

bool LightEffects::RenderStaticLights()
{
    return false;
}

void LightEffects::RenderSpecular()
{
}

bool LightEffects::RenderSpecular_Dynamic()
{
    return false;
}

bool LightEffects::RenderSpecular_Static()
{
    return false;
}

bool LightEffects::RenderEnvironment()
{
    return false;
}

void LightEffects::RenderBump()
{
}

bool LightEffects::CreateNormalizationCubeMap(unsigned long dwSize, void* ppCubeMap)
{
    return false;
}

void LightEffects::StartLightPhase()
{
}

void LightEffects::EndLightPhase()
{
}
