//
//
// Win_HighDynamicRange.h
//
// Declaration of high dynamic range effect class
//
//
#ifndef _WIN_HIGHDYNAMICRANGE_H_
#define _WIN_HIGHDYNAMICRANGE_H_
struct FilterSample
{
    float fValue;               // coefficient
    float fOffsetX, fOffsetY;   // subpixel offsets of supersamples in
                                //   destination coordinates
};
class VVHighDynamicRange
{
public:
    virtual void Initialize();
    virtual void Render();
    VVHighDynamicRange();
};
extern VVHighDynamicRange HDREffect;
#endif
