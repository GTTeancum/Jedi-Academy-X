//
//
// win_lightefects.h
//
// Declaration of class for pixel shader light effects
//
//
#ifndef _WIN_LIGHTEFFECTS_H_
#define _WIN_LIGHTEFFECTS_H_
class LightEffects
{
public:
    LightEffects();
    virtual ~LightEffects();
    bool Initialize();
    void ProcessVertices(void* pDirLightDir, void* pPtLightPos);
    bool RenderDynamicLights();
    bool RenderStaticLights();
    void RenderSpecular();
    bool RenderSpecular_Dynamic();
    bool RenderSpecular_Static();
    bool RenderEnvironment();
    void RenderBump();
    bool CreateNormalizationCubeMap(unsigned long dwSize, void* ppCubeMap);
    void StartLightPhase();
    void EndLightPhase();
};
#endif
