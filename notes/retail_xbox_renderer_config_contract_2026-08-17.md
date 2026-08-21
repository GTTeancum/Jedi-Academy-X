# Retail Xbox Renderer Configuration Contract

The retail renderer is defined by both the `jamp.xbe` code and the renderer
settings shipped beside it. Source-level parity alone is not enough when the
package overrides a more expensive source default.

## Authority

- Retail SP config: `Jedi-Academy-X/Star Wars Jedi Academy game/base/default.cfg`
- Retail MP config: `Jedi-Academy-X/Star Wars Jedi Academy game/base/mpdefault.cfg`
- Retail MP source reference: `Jedi-Academy-X/clean-mp-original-build/codemp/renderer/tr_init.cpp`
- Shared EF renderer registration: `code/renderer/tr_init.cpp`

## Performance-Relevant Matrix

| Setting | Retail SP | Retail MP | Current shared runtime | Decision |
| --- | ---: | ---: | ---: | --- |
| `r_ext_texture_filter_anisotropic` | source default 16 | 1 | 1 | Use the explicit retail MP console override. |
| `r_picmip` | 1 | 1 | 1 | Use one downsample level for both EF personalities. |
| `r_detailtextures` | 1 | 1 | 1 | Match retail. |
| `r_simpleMipMaps` | 1 | 1 | 1 | Match retail. |
| `r_vertexLight` | 0 | 0 | 0 | Match retail lightmapped rendering. |
| `r_subdivisions` | 4 | 4 | 64 | Retain the coarser Xbox workload reduction for now. |
| `r_lodCurveError` | 250 | 250 | 250 | Match retail. |
| `r_lodbias` | 0 | 0 | 0 | Match retail. |
| `r_dynamiclight` | 1 | 1 | 1 | Match retail. |
| `r_dlightBacks` | 1 | 1 | 0 | Retain the lower-cost current policy. |
| `r_finish` | 0 | 0 | 0 | Match retail; no forced GPU synchronization. |
| `r_textureMode` | trilinear | trilinear | bilinear mip selection | Retain the lower-cost current policy. |
| `r_swapInterval` | 0 | 0 | 0 | Match retail; no vsync wait. |
| `r_facePlaneCull` | 1 | 1 | 1 | Match retail. |
| `r_primitives` | 0 | 0 | 0 | Match retail automatic primitive selection. |
| `cg_shadows` | 2 | 1 | 1 | Match retail MP and avoid the more expensive SP mode. |

## Conclusion

The audit found two workload-increasing package drifts: the renderer source
default requested anisotropy above retail MP's explicit 1x setting, and the
Holomatch personality forced `r_picmip 0` while both retail Xbox configs use
`r_picmip 1`. The shared runtime now applies those two retail policies to SP
and Holomatch. Deliberate settings that reduce work remain in place until
visual parity or measured hardware evidence requires changing them.

