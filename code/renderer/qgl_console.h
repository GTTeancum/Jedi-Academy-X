/*
** QGL.H
*/

#ifndef __QGL_H__
#define __QGL_H__

#ifdef _WINDOWS
#include <windows.h>
#include <gl/gl.h>
#endif

#ifdef _XBOX

#include <Xtl.h>
#endif

#ifdef _GAMECUBE
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;

#undef APIENTRY
#define APIENTRY

#define GL_ACCUM                          0x0100
#define GL_LOAD                           0x0101
#define GL_RETURN                         0x0102
#define GL_MULT                           0x0103
#define GL_ADD                            0x0104

/* AlphaFunction */
#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

/* AttribMask */
#define GL_CURRENT_BIT                    0x00000001
#define GL_POINT_BIT                      0x00000002
#define GL_LINE_BIT                       0x00000004
#define GL_POLYGON_BIT                    0x00000008
#define GL_POLYGON_STIPPLE_BIT            0x00000010
#define GL_PIXEL_MODE_BIT                 0x00000020
#define GL_LIGHTING_BIT                   0x00000040
#define GL_FOG_BIT                        0x00000080
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_ACCUM_BUFFER_BIT               0x00000200
#define GL_STENCIL_BUFFER_BIT             0x00000400
#define GL_VIEWPORT_BIT                   0x00000800
#define GL_TRANSFORM_BIT                  0x00001000
#define GL_ENABLE_BIT                     0x00002000
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_HINT_BIT                       0x00008000
#define GL_EVAL_BIT                       0x00010000
#define GL_LIST_BIT                       0x00020000
#define GL_TEXTURE_BIT                    0x00040000
#define GL_SCISSOR_BIT                    0x00080000
#define GL_ALL_ATTRIB_BITS                0x000fffff

/* BeginMode */
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006
#define GL_QUADS                          0x0007
#define GL_QUAD_STRIP                     0x0008
#define GL_POLYGON                        0x0009

/* BlendingFactorDest */
#define GL_ZERO                           0
#define GL_ONE                            1
#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305

/* BlendingFactorSrc */
/*      GL_ZERO */
/*      GL_ONE */
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307
#define GL_SRC_ALPHA_SATURATE             0x0308

/* Boolean */
#define GL_TRUE                           1
#define GL_FALSE                          0

/* ClipPlaneName */
#define GL_CLIP_PLANE0                    0x3000
#define GL_CLIP_PLANE1                    0x3001
#define GL_CLIP_PLANE2                    0x3002
#define GL_CLIP_PLANE3                    0x3003
#define GL_CLIP_PLANE4                    0x3004
#define GL_CLIP_PLANE5                    0x3005

/* DataType */
#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_2_BYTES                        0x1407
#define GL_3_BYTES                        0x1408
#define GL_4_BYTES                        0x1409
#define GL_DOUBLE                         0x140A

/* DrawBufferMode */
#define GL_NONE                           0
#define GL_FRONT_LEFT                     0x0400
#define GL_FRONT_RIGHT                    0x0401
#define GL_BACK_LEFT                      0x0402
#define GL_BACK_RIGHT                     0x0403
#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_LEFT                           0x0406
#define GL_RIGHT                          0x0407
#define GL_FRONT_AND_BACK                 0x0408
#define GL_AUX0                           0x0409
#define GL_AUX1                           0x040A
#define GL_AUX2                           0x040B
#define GL_AUX3                           0x040C

/* ErrorCode */
#define GL_NO_ERROR                       0
#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_STACK_OVERFLOW                 0x0503
#define GL_STACK_UNDERFLOW                0x0504
#define GL_OUT_OF_MEMORY                  0x0505

/* FeedBackMode */
#define GL_2D                             0x0600
#define GL_3D                             0x0601
#define GL_3D_COLOR                       0x0602
#define GL_3D_COLOR_TEXTURE               0x0603
#define GL_4D_COLOR_TEXTURE               0x0604

/* FeedBackToken */
#define GL_PASS_THROUGH_TOKEN             0x0700
#define GL_POINT_TOKEN                    0x0701
#define GL_LINE_TOKEN                     0x0702
#define GL_POLYGON_TOKEN                  0x0703
#define GL_BITMAP_TOKEN                   0x0704
#define GL_DRAW_PIXEL_TOKEN               0x0705
#define GL_COPY_PIXEL_TOKEN               0x0706
#define GL_LINE_RESET_TOKEN               0x0707

/* FogMode */
/*      GL_LINEAR */
#define GL_EXP                            0x0800
#define GL_EXP2                           0x0801

/* FrontFaceDirection */
#define GL_CW                             0x0900
#define GL_CCW                            0x0901

/* GetMapTarget */
#define GL_COEFF                          0x0A00
#define GL_ORDER                          0x0A01
#define GL_DOMAIN                         0x0A02

/* GetTarget */
#define GL_CURRENT_COLOR                  0x0B00
#define GL_CURRENT_INDEX                  0x0B01
#define GL_CURRENT_NORMAL                 0x0B02
#define GL_CURRENT_TEXTURE_COORDS         0x0B03
#define GL_CURRENT_RASTER_COLOR           0x0B04
#define GL_CURRENT_RASTER_INDEX           0x0B05
#define GL_CURRENT_RASTER_TEXTURE_COORDS  0x0B06
#define GL_CURRENT_RASTER_POSITION        0x0B07
#define GL_CURRENT_RASTER_POSITION_VALID  0x0B08
#define GL_CURRENT_RASTER_DISTANCE        0x0B09
#define GL_POINT_SMOOTH                   0x0B10
#define GL_POINT_SIZE                     0x0B11
#define GL_POINT_SIZE_RANGE               0x0B12
#define GL_POINT_SIZE_GRANULARITY         0x0B13
#define GL_LINE_SMOOTH                    0x0B20
#define GL_LINE_WIDTH                     0x0B21
#define GL_LINE_WIDTH_RANGE               0x0B22
#define GL_LINE_WIDTH_GRANULARITY         0x0B23
#define GL_LINE_STIPPLE                   0x0B24
#define GL_LINE_STIPPLE_PATTERN           0x0B25
#define GL_LINE_STIPPLE_REPEAT            0x0B26
#define GL_LIST_MODE                      0x0B30
#define GL_MAX_LIST_NESTING               0x0B31
#define GL_LIST_BASE                      0x0B32
#define GL_LIST_INDEX                     0x0B33
#define GL_POLYGON_MODE                   0x0B40
#define GL_POLYGON_SMOOTH                 0x0B41
#define GL_POLYGON_STIPPLE                0x0B42
#define GL_EDGE_FLAG                      0x0B43
#define GL_CULL_FACE                      0x0B44
#define GL_CULL_FACE_MODE                 0x0B45
#define GL_FRONT_FACE                     0x0B46
#define GL_LIGHTING                       0x0B50
#define GL_LIGHT_MODEL_LOCAL_VIEWER       0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE           0x0B52
#define GL_LIGHT_MODEL_AMBIENT            0x0B53
#define GL_SHADE_MODEL                    0x0B54
#define GL_COLOR_MATERIAL_FACE            0x0B55
#define GL_COLOR_MATERIAL_PARAMETER       0x0B56
#define GL_COLOR_MATERIAL                 0x0B57
#define GL_FOG                            0x0B60
#define GL_FOG_INDEX                      0x0B61
#define GL_FOG_DENSITY                    0x0B62
#define GL_FOG_START                      0x0B63
#define GL_FOG_END                        0x0B64
#define GL_FOG_MODE                       0x0B65
#define GL_FOG_COLOR                      0x0B66
#define GL_DEPTH_RANGE                    0x0B70
#define GL_DEPTH_TEST                     0x0B71
#define GL_DEPTH_WRITEMASK                0x0B72
#define GL_DEPTH_CLEAR_VALUE              0x0B73
#define GL_DEPTH_FUNC                     0x0B74
#define GL_ACCUM_CLEAR_VALUE              0x0B80
#define GL_STENCIL_TEST                   0x0B90
#define GL_STENCIL_CLEAR_VALUE            0x0B91
#define GL_STENCIL_FUNC                   0x0B92
#define GL_STENCIL_VALUE_MASK             0x0B93
#define GL_STENCIL_FAIL                   0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL        0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS        0x0B96
#define GL_STENCIL_REF                    0x0B97
#define GL_STENCIL_WRITEMASK              0x0B98
#define GL_MATRIX_MODE                    0x0BA0
#define GL_NORMALIZE                      0x0BA1
#define GL_VIEWPORT                       0x0BA2
#define GL_MODELVIEW_STACK_DEPTH          0x0BA3
#define GL_PROJECTION_STACK_DEPTH         0x0BA4
#define GL_TEXTURE_STACK_DEPTH            0x0BA5
#define GL_MODELVIEW_MATRIX               0x0BA6
#define GL_PROJECTION_MATRIX              0x0BA7
#define GL_TEXTURE_MATRIX                 0x0BA8
#define GL_ATTRIB_STACK_DEPTH             0x0BB0
#define GL_CLIENT_ATTRIB_STACK_DEPTH      0x0BB1
#define GL_ALPHA_TEST                     0x0BC0
#define GL_ALPHA_TEST_FUNC                0x0BC1
#define GL_ALPHA_TEST_REF                 0x0BC2
#define GL_DITHER                         0x0BD0
#define GL_BLEND_DST                      0x0BE0
#define GL_BLEND_SRC                      0x0BE1
#define GL_BLEND                          0x0BE2
#define GL_LOGIC_OP_MODE                  0x0BF0
#define GL_INDEX_LOGIC_OP                 0x0BF1
#define GL_COLOR_LOGIC_OP                 0x0BF2
#define GL_AUX_BUFFERS                    0x0C00
#define GL_DRAW_BUFFER                    0x0C01
#define GL_READ_BUFFER                    0x0C02
#define GL_SCISSOR_BOX                    0x0C10
#define GL_SCISSOR_TEST                   0x0C11
#define GL_INDEX_CLEAR_VALUE              0x0C20
#define GL_INDEX_WRITEMASK                0x0C21
#define GL_COLOR_CLEAR_VALUE              0x0C22
#define GL_COLOR_WRITEMASK                0x0C23
#define GL_INDEX_MODE                     0x0C30
#define GL_RGBA_MODE                      0x0C31
#define GL_DOUBLEBUFFER                   0x0C32
#define GL_STEREO                         0x0C33
#define GL_RENDER_MODE                    0x0C40
#define GL_PERSPECTIVE_CORRECTION_HINT    0x0C50
#define GL_POINT_SMOOTH_HINT              0x0C51
#define GL_LINE_SMOOTH_HINT               0x0C52
#define GL_POLYGON_SMOOTH_HINT            0x0C53
#define GL_FOG_HINT                       0x0C54
#define GL_TEXTURE_GEN_S                  0x0C60
#define GL_TEXTURE_GEN_T                  0x0C61
#define GL_TEXTURE_GEN_R                  0x0C62
#define GL_TEXTURE_GEN_Q                  0x0C63
#define GL_PIXEL_MAP_I_TO_I               0x0C70
#define GL_PIXEL_MAP_S_TO_S               0x0C71
#define GL_PIXEL_MAP_I_TO_R               0x0C72
#define GL_PIXEL_MAP_I_TO_G               0x0C73
#define GL_PIXEL_MAP_I_TO_B               0x0C74
#define GL_PIXEL_MAP_I_TO_A               0x0C75
#define GL_PIXEL_MAP_R_TO_R               0x0C76
#define GL_PIXEL_MAP_G_TO_G               0x0C77
#define GL_PIXEL_MAP_B_TO_B               0x0C78
#define GL_PIXEL_MAP_A_TO_A               0x0C79
#define GL_PIXEL_MAP_I_TO_I_SIZE          0x0CB0
#define GL_PIXEL_MAP_S_TO_S_SIZE          0x0CB1
#define GL_PIXEL_MAP_I_TO_R_SIZE          0x0CB2
#define GL_PIXEL_MAP_I_TO_G_SIZE          0x0CB3
#define GL_PIXEL_MAP_I_TO_B_SIZE          0x0CB4
#define GL_PIXEL_MAP_I_TO_A_SIZE          0x0CB5
#define GL_PIXEL_MAP_R_TO_R_SIZE          0x0CB6
#define GL_PIXEL_MAP_G_TO_G_SIZE          0x0CB7
#define GL_PIXEL_MAP_B_TO_B_SIZE          0x0CB8
#define GL_PIXEL_MAP_A_TO_A_SIZE          0x0CB9
#define GL_UNPACK_SWAP_BYTES              0x0CF0
#define GL_UNPACK_LSB_FIRST               0x0CF1
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_UNPACK_SKIP_ROWS               0x0CF3
#define GL_UNPACK_SKIP_PIXELS             0x0CF4
#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_PACK_SWAP_BYTES                0x0D00
#define GL_PACK_LSB_FIRST                 0x0D01
#define GL_PACK_ROW_LENGTH                0x0D02
#define GL_PACK_SKIP_ROWS                 0x0D03
#define GL_PACK_SKIP_PIXELS               0x0D04
#define GL_PACK_ALIGNMENT                 0x0D05
#define GL_MAP_COLOR                      0x0D10
#define GL_MAP_STENCIL                    0x0D11
#define GL_INDEX_SHIFT                    0x0D12
#define GL_INDEX_OFFSET                   0x0D13
#define GL_RED_SCALE                      0x0D14
#define GL_RED_BIAS                       0x0D15
#define GL_ZOOM_X                         0x0D16
#define GL_ZOOM_Y                         0x0D17
#define GL_GREEN_SCALE                    0x0D18
#define GL_GREEN_BIAS                     0x0D19
#define GL_BLUE_SCALE                     0x0D1A
#define GL_BLUE_BIAS                      0x0D1B
#define GL_ALPHA_SCALE                    0x0D1C
#define GL_ALPHA_BIAS                     0x0D1D
#define GL_DEPTH_SCALE                    0x0D1E
#define GL_DEPTH_BIAS                     0x0D1F
#define GL_MAX_EVAL_ORDER                 0x0D30
#define GL_MAX_LIGHTS                     0x0D31
#define GL_MAX_CLIP_PLANES                0x0D32
#define GL_MAX_TEXTURE_SIZE               0x0D33
#define GL_MAX_PIXEL_MAP_TABLE            0x0D34
#define GL_MAX_ATTRIB_STACK_DEPTH         0x0D35
#define GL_MAX_MODELVIEW_STACK_DEPTH      0x0D36
#define GL_MAX_NAME_STACK_DEPTH           0x0D37
#define GL_MAX_PROJECTION_STACK_DEPTH     0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH        0x0D39
#define GL_MAX_VIEWPORT_DIMS              0x0D3A
#define GL_MAX_CLIENT_ATTRIB_STACK_DEPTH  0x0D3B
#define GL_SUBPIXEL_BITS                  0x0D50
#define GL_INDEX_BITS                     0x0D51
#define GL_RED_BITS                       0x0D52
#define GL_GREEN_BITS                     0x0D53
#define GL_BLUE_BITS                      0x0D54
#define GL_ALPHA_BITS                     0x0D55
#define GL_DEPTH_BITS                     0x0D56
#define GL_STENCIL_BITS                   0x0D57
#define GL_ACCUM_RED_BITS                 0x0D58
#define GL_ACCUM_GREEN_BITS               0x0D59
#define GL_ACCUM_BLUE_BITS                0x0D5A
#define GL_ACCUM_ALPHA_BITS               0x0D5B
#define GL_NAME_STACK_DEPTH               0x0D70
#define GL_AUTO_NORMAL                    0x0D80
#define GL_MAP1_COLOR_4                   0x0D90
#define GL_MAP1_INDEX                     0x0D91
#define GL_MAP1_NORMAL                    0x0D92
#define GL_MAP1_TEXTURE_COORD_1           0x0D93
#define GL_MAP1_TEXTURE_COORD_2           0x0D94
#define GL_MAP1_TEXTURE_COORD_3           0x0D95
#define GL_MAP1_TEXTURE_COORD_4           0x0D96
#define GL_MAP1_VERTEX_3                  0x0D97
#define GL_MAP1_VERTEX_4                  0x0D98
#define GL_MAP2_COLOR_4                   0x0DB0
#define GL_MAP2_INDEX                     0x0DB1
#define GL_MAP2_NORMAL                    0x0DB2
#define GL_MAP2_TEXTURE_COORD_1           0x0DB3
#define GL_MAP2_TEXTURE_COORD_2           0x0DB4
#define GL_MAP2_TEXTURE_COORD_3           0x0DB5
#define GL_MAP2_TEXTURE_COORD_4           0x0DB6
#define GL_MAP2_VERTEX_3                  0x0DB7
#define GL_MAP2_VERTEX_4                  0x0DB8
#define GL_MAP1_GRID_DOMAIN               0x0DD0
#define GL_MAP1_GRID_SEGMENTS             0x0DD1
#define GL_MAP2_GRID_DOMAIN               0x0DD2
#define GL_MAP2_GRID_SEGMENTS             0x0DD3
#define GL_TEXTURE_1D                     0x0DE0
#define GL_TEXTURE_2D                     0x0DE1
#define GL_FEEDBACK_BUFFER_POINTER        0x0DF0
#define GL_FEEDBACK_BUFFER_SIZE           0x0DF1
#define GL_FEEDBACK_BUFFER_TYPE           0x0DF2
#define GL_SELECTION_BUFFER_POINTER       0x0DF3
#define GL_SELECTION_BUFFER_SIZE          0x0DF4

/* GetTextureParameter */
#define GL_TEXTURE_WIDTH                  0x1000
#define GL_TEXTURE_HEIGHT                 0x1001
#define GL_TEXTURE_INTERNAL_FORMAT        0x1003
#define GL_TEXTURE_BORDER_COLOR           0x1004
#define GL_TEXTURE_BORDER                 0x1005

/* HintMode */
#define GL_DONT_CARE                      0x1100
#define GL_FASTEST                        0x1101
#define GL_NICEST                         0x1102

/* LightName */
#define GL_LIGHT0                         0x4000
#define GL_LIGHT1                         0x4001
#define GL_LIGHT2                         0x4002
#define GL_LIGHT3                         0x4003
#define GL_LIGHT4                         0x4004
#define GL_LIGHT5                         0x4005
#define GL_LIGHT6                         0x4006
#define GL_LIGHT7                         0x4007

/* LightParameter */
#define GL_AMBIENT                        0x1200
#define GL_DIFFUSE                        0x1201
#define GL_SPECULAR                       0x1202
#define GL_POSITION                       0x1203
#define GL_SPOT_DIRECTION                 0x1204
#define GL_SPOT_EXPONENT                  0x1205
#define GL_SPOT_CUTOFF                    0x1206
#define GL_CONSTANT_ATTENUATION           0x1207
#define GL_LINEAR_ATTENUATION             0x1208
#define GL_QUADRATIC_ATTENUATION          0x1209

/* ListMode */
#define GL_COMPILE                        0x1300
#define GL_COMPILE_AND_EXECUTE            0x1301

/* LogicOp */
#define GL_CLEAR                          0x1500
#define GL_AND                            0x1501
#define GL_AND_REVERSE                    0x1502
#define GL_COPY                           0x1503
#define GL_AND_INVERTED                   0x1504
#define GL_NOOP                           0x1505
#define GL_XOR                            0x1506
#define GL_OR                             0x1507
#define GL_NOR                            0x1508
#define GL_EQUIV                          0x1509
#define GL_INVERT                         0x150A
#define GL_OR_REVERSE                     0x150B
#define GL_COPY_INVERTED                  0x150C
#define GL_OR_INVERTED                    0x150D
#define GL_NAND                           0x150E
#define GL_SET                            0x150F

/* MaterialParameter */
#define GL_EMISSION                       0x1600
#define GL_SHININESS                      0x1601
#define GL_AMBIENT_AND_DIFFUSE            0x1602
#define GL_COLOR_INDEXES                  0x1603

/* MatrixMode */
#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_TEXTURE0						  0x1702
#define GL_TEXTURE1                       0x1703
#define GL_TEXTURE2                       0x1704
#define GL_TEXTURE3                       0x1705

/* PixelCopyType */
#define GL_COLOR                          0x1800
#define GL_DEPTH                          0x1801
#define GL_STENCIL                        0x1802

/* PixelFormat */
#define GL_COLOR_INDEX                    0x1900
#define GL_STENCIL_INDEX                  0x1901
#define GL_DEPTH_COMPONENT                0x1902
#define GL_RED                            0x1903
#define GL_GREEN                          0x1904
#define GL_BLUE                           0x1905
#define GL_ALPHA                          0x1906
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_LUMINANCE                      0x1909
#define GL_LUMINANCE_ALPHA                0x190A

/* PixelType */
#define GL_BITMAP                         0x1A00

/* PolygonMode */
#define GL_POINT                          0x1B00
#define GL_LINE                           0x1B01
#define GL_FILL                           0x1B02

/* RenderingMode */
#define GL_RENDER                         0x1C00
#define GL_FEEDBACK                       0x1C01
#define GL_SELECT                         0x1C02

/* ShadingModel */
#define GL_FLAT                           0x1D00
#define GL_SMOOTH                         0x1D01

/* StencilOp */
/*      GL_ZERO */
#define GL_KEEP                           0x1E00
#define GL_REPLACE                        0x1E01
#define GL_INCR                           0x1E02
#define GL_DECR                           0x1E03
/*      GL_INVERT */

/* StringName */
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_EXTENSIONS                     0x1F03

/* TextureCoordName */
#define GL_S                              0x2000
#define GL_T                              0x2001
#define GL_R                              0x2002
#define GL_Q                              0x2003

/* TextureEnvMode */
#define GL_MODULATE                       0x2100
#define GL_DECAL                          0x2101

/* TextureEnvParameter */
#define GL_TEXTURE_ENV_MODE               0x2200
#define GL_TEXTURE_ENV_COLOR              0x2201

/* TextureEnvTarget */
#define GL_TEXTURE_ENV                    0x2300

/* TextureGenMode */
#define GL_EYE_LINEAR                     0x2400
#define GL_OBJECT_LINEAR                  0x2401
#define GL_SPHERE_MAP                     0x2402

/* TextureGenParameter */
#define GL_TEXTURE_GEN_MODE               0x2500
#define GL_OBJECT_PLANE                   0x2501
#define GL_EYE_PLANE                      0x2502

/* TextureMagFilter */
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601

/* TextureMinFilter */
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

/* TextureParameterName */
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803

// PORT: Anisotropy stuff
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

//PORT - TPL stuff
#define GL_TPL4_EXT                       0x9991
#define GL_TPL8_EXT                       0x9992
#define GL_TPL16_EXT                      0x9993
#define GL_TPL32_EXT                      0x9994

// PORT: DDS Stuff
#define GL_DDS_RGBA_EXT                   0x9998
#define GL_RGB_SWIZZLE_EXT				  0x9999

/* TextureWrapMode */
#define GL_CLAMP                          0x2900
#define GL_REPEAT                         0x2901

/* ClientAttribMask */
#define GL_CLIENT_PIXEL_STORE_BIT         0x00000001
#define GL_CLIENT_VERTEX_ARRAY_BIT        0x00000002
#define GL_CLIENT_ALL_ATTRIB_BITS         0xffffffff

/* polygon_offset */
#define GL_POLYGON_OFFSET_FACTOR          0x8038
#define GL_POLYGON_OFFSET_UNITS           0x2A00
#define GL_POLYGON_OFFSET_POINT           0x2A01
#define GL_POLYGON_OFFSET_LINE            0x2A02
#define GL_POLYGON_OFFSET_FILL            0x8037

/* texture */
#define GL_ALPHA4                         0x803B
#define GL_ALPHA8                         0x803C
#define GL_ALPHA12                        0x803D
#define GL_ALPHA16                        0x803E
#define GL_LUMINANCE4                     0x803F
#define GL_LUMINANCE8                     0x8040
#define GL_LUMINANCE12                    0x8041
#define GL_LUMINANCE16                    0x8042
#define GL_LUMINANCE4_ALPHA4              0x8043
#define GL_LUMINANCE6_ALPHA2              0x8044
#define GL_LUMINANCE8_ALPHA8              0x8045
#define GL_LUMINANCE12_ALPHA4             0x8046
#define GL_LUMINANCE12_ALPHA12            0x8047
#define GL_LUMINANCE16_ALPHA16            0x8048
#define GL_INTENSITY                      0x8049
#define GL_INTENSITY4                     0x804A
#define GL_INTENSITY8                     0x804B
#define GL_INTENSITY12                    0x804C
#define GL_INTENSITY16                    0x804D
#define GL_R3_G3_B2                       0x2A10
#define GL_RGB4                           0x804F
#define GL_RGB5                           0x8050
#define GL_RGB8                           0x8051
#define GL_RGB10                          0x8052
#define GL_RGB12                          0x8053
#define GL_RGB16                          0x8054
#define GL_RGBA2                          0x8055
#define GL_RGBA4                          0x8056
#define GL_RGB5_A1                        0x8057
#define GL_RGBA8                          0x8058
#define GL_RGB10_A2                       0x8059
#define GL_RGBA12                         0x805A
#define GL_RGBA16                         0x805B
#define GL_TEXTURE_RED_SIZE               0x805C
#define GL_TEXTURE_GREEN_SIZE             0x805D
#define GL_TEXTURE_BLUE_SIZE              0x805E
#define GL_TEXTURE_ALPHA_SIZE             0x805F
#define GL_TEXTURE_LUMINANCE_SIZE         0x8060
#define GL_TEXTURE_INTENSITY_SIZE         0x8061
#define GL_PROXY_TEXTURE_1D               0x8063
#define GL_PROXY_TEXTURE_2D               0x8064

/* texture_object */
#define GL_TEXTURE_PRIORITY               0x8066
#define GL_TEXTURE_RESIDENT               0x8067
#define GL_TEXTURE_BINDING_1D             0x8068
#define GL_TEXTURE_BINDING_2D             0x8069

/* vertex_array */
#define GL_VERTEX_ARRAY                   0x8074
#define GL_NORMAL_ARRAY                   0x8075
#define GL_COLOR_ARRAY                    0x8076
#define GL_INDEX_ARRAY                    0x8077
#define GL_TEXTURE_COORD_ARRAY            0x8078
#define GL_EDGE_FLAG_ARRAY                0x8079
#define GL_VERTEX_ARRAY_SIZE              0x807A
#define GL_VERTEX_ARRAY_TYPE              0x807B
#define GL_VERTEX_ARRAY_STRIDE            0x807C
#define GL_NORMAL_ARRAY_TYPE              0x807E
#define GL_NORMAL_ARRAY_STRIDE            0x807F
#define GL_COLOR_ARRAY_SIZE               0x8081
#define GL_COLOR_ARRAY_TYPE               0x8082
#define GL_COLOR_ARRAY_STRIDE             0x8083
#define GL_INDEX_ARRAY_TYPE               0x8085
#define GL_INDEX_ARRAY_STRIDE             0x8086
#define GL_TEXTURE_COORD_ARRAY_SIZE       0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE       0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE     0x808A
#define GL_EDGE_FLAG_ARRAY_STRIDE         0x808C
#define GL_VERTEX_ARRAY_POINTER           0x808E
#define GL_NORMAL_ARRAY_POINTER           0x808F
#define GL_COLOR_ARRAY_POINTER            0x8090
#define GL_INDEX_ARRAY_POINTER            0x8091
#define GL_TEXTURE_COORD_ARRAY_POINTER    0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER        0x8093
#define GL_V2F                            0x2A20
#define GL_V3F                            0x2A21
#define GL_C4UB_V2F                       0x2A22
#define GL_C4UB_V3F                       0x2A23
#define GL_C3F_V3F                        0x2A24
#define GL_N3F_V3F                        0x2A25
#define GL_C4F_N3F_V3F                    0x2A26
#define GL_T2F_V3F                        0x2A27
#define GL_T4F_V4F                        0x2A28
#define GL_T2F_C4UB_V3F                   0x2A29
#define GL_T2F_C3F_V3F                    0x2A2A
#define GL_T2F_N3F_V3F                    0x2A2B
#define GL_T2F_C4F_N3F_V3F                0x2A2C
#define GL_T4F_C4F_N3F_V4F                0x2A2D

/* Extensions */
#define GL_EXT_vertex_array               1
#define GL_EXT_bgra                       1
#define GL_EXT_paletted_texture           1

/* EXT_vertex_array */
#define GL_VERTEX_ARRAY_EXT               0x8074
#define GL_NORMAL_ARRAY_EXT               0x8075
#define GL_COLOR_ARRAY_EXT                0x8076
#define GL_INDEX_ARRAY_EXT                0x8077
#define GL_TEXTURE_COORD_ARRAY_EXT        0x8078
#define GL_EDGE_FLAG_ARRAY_EXT            0x8079
#define GL_VERTEX_ARRAY_SIZE_EXT          0x807A
#define GL_VERTEX_ARRAY_TYPE_EXT          0x807B
#define GL_VERTEX_ARRAY_STRIDE_EXT        0x807C
#define GL_VERTEX_ARRAY_COUNT_EXT         0x807D
#define GL_NORMAL_ARRAY_TYPE_EXT          0x807E
#define GL_NORMAL_ARRAY_STRIDE_EXT        0x807F
#define GL_NORMAL_ARRAY_COUNT_EXT         0x8080
#define GL_COLOR_ARRAY_SIZE_EXT           0x8081
#define GL_COLOR_ARRAY_TYPE_EXT           0x8082
#define GL_COLOR_ARRAY_STRIDE_EXT         0x8083
#define GL_COLOR_ARRAY_COUNT_EXT          0x8084
#define GL_INDEX_ARRAY_TYPE_EXT           0x8085
#define GL_INDEX_ARRAY_STRIDE_EXT         0x8086
#define GL_INDEX_ARRAY_COUNT_EXT          0x8087
#define GL_TEXTURE_COORD_ARRAY_SIZE_EXT   0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE_EXT   0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE_EXT 0x808A
#define GL_TEXTURE_COORD_ARRAY_COUNT_EXT  0x808B
#define GL_EDGE_FLAG_ARRAY_STRIDE_EXT     0x808C
#define GL_EDGE_FLAG_ARRAY_COUNT_EXT      0x808D
#define GL_VERTEX_ARRAY_POINTER_EXT       0x808E
#define GL_NORMAL_ARRAY_POINTER_EXT       0x808F
#define GL_COLOR_ARRAY_POINTER_EXT        0x8090
#define GL_INDEX_ARRAY_POINTER_EXT        0x8091
#define GL_TEXTURE_COORD_ARRAY_POINTER_EXT 0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER_EXT    0x8093
#define GL_DOUBLE_EXT                     GL_DOUBLE

/* EXT_bgra */
#define GL_BGR_EXT                        0x80E0
#define GL_BGRA_EXT                       0x80E1

/* EXT_paletted_texture */

/* These must match the GL_COLOR_TABLE_*_SGI enumerants */
#define GL_COLOR_TABLE_FORMAT_EXT         0x80D8
#define GL_COLOR_TABLE_WIDTH_EXT          0x80D9
#define GL_COLOR_TABLE_RED_SIZE_EXT       0x80DA
#define GL_COLOR_TABLE_GREEN_SIZE_EXT     0x80DB
#define GL_COLOR_TABLE_BLUE_SIZE_EXT      0x80DC
#define GL_COLOR_TABLE_ALPHA_SIZE_EXT     0x80DD
#define GL_COLOR_TABLE_LUMINANCE_SIZE_EXT 0x80DE
#define GL_COLOR_TABLE_INTENSITY_SIZE_EXT 0x80DF

#define GL_COLOR_INDEX1_EXT               0x80E2
#define GL_COLOR_INDEX2_EXT               0x80E3
#define GL_COLOR_INDEX4_EXT               0x80E4
#define GL_COLOR_INDEX8_EXT               0x80E5
#define GL_COLOR_INDEX12_EXT              0x80E6
#define GL_COLOR_INDEX16_EXT              0x80E7

// VVFIXME New Constants from Jedi
#define GL_VSYNC                          0x813F
#define GL_DDS_RGB16_EXT                  0x9997
#define GL_DDS_RGBA32_EXT                 0x9998
#define GL_RGB_SWIZZLE_EXT                0x9999

//	VVFIXME - New constants for linear format textures.
// These numbers are just made up. This is awful.
#define GL_LIN_RGBA8	0x8E01
#define GL_LIN_RGBA		0x8E02
#define GL_LIN_RGB8		0x8E03
#define GL_LIN_RGB		0x8E04

//===========================================================================

/*
** multitexture extension definitions
*/
#define GL_ACTIVE_TEXTURE_ARB               0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB        0x84E1
#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#define GL_TEXTURE0_ARB                     0x84C0
#define GL_TEXTURE1_ARB                     0x84C1
#define GL_TEXTURE2_ARB                     0x84C2
#define GL_TEXTURE3_ARB                     0x84C3

typedef void ( * PFNGLMULTITEXCOORD1DARBPROC) (GLenum target, GLdouble s);
typedef void ( * PFNGLMULTITEXCOORD1DVARBPROC) (GLenum target, const GLdouble *v);
typedef void ( * PFNGLMULTITEXCOORD1FARBPROC) (GLenum target, GLfloat s);
typedef void ( * PFNGLMULTITEXCOORD1FVARBPROC) (GLenum target, const GLfloat *v);
typedef void ( * PFNGLMULTITEXCOORD1IARBPROC) (GLenum target, GLint s);
typedef void ( * PFNGLMULTITEXCOORD1IVARBPROC) (GLenum target, const GLint *v);
typedef void ( * PFNGLMULTITEXCOORD1SARBPROC) (GLenum target, GLshort s);
typedef void ( * PFNGLMULTITEXCOORD1SVARBPROC) (GLenum target, const GLshort *v);
typedef void ( * PFNGLMULTITEXCOORD2DARBPROC) (GLenum target, GLdouble s, GLdouble t);
typedef void ( * PFNGLMULTITEXCOORD2DVARBPROC) (GLenum target, const GLdouble *v);
typedef void ( * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void ( * PFNGLMULTITEXCOORD2FVARBPROC) (GLenum target, const GLfloat *v);
typedef void ( * PFNGLMULTITEXCOORD2IARBPROC) (GLenum target, GLint s, GLint t);
typedef void ( * PFNGLMULTITEXCOORD2IVARBPROC) (GLenum target, const GLint *v);
typedef void ( * PFNGLMULTITEXCOORD2SARBPROC) (GLenum target, GLshort s, GLshort t);
typedef void ( * PFNGLMULTITEXCOORD2SVARBPROC) (GLenum target, const GLshort *v);
typedef void ( * PFNGLMULTITEXCOORD3DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void ( * PFNGLMULTITEXCOORD3DVARBPROC) (GLenum target, const GLdouble *v);
typedef void ( * PFNGLMULTITEXCOORD3FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void ( * PFNGLMULTITEXCOORD3FVARBPROC) (GLenum target, const GLfloat *v);
typedef void ( * PFNGLMULTITEXCOORD3IARBPROC) (GLenum target, GLint s, GLint t, GLint r);
typedef void ( * PFNGLMULTITEXCOORD3IVARBPROC) (GLenum target, const GLint *v);
typedef void ( * PFNGLMULTITEXCOORD3SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r);
typedef void ( * PFNGLMULTITEXCOORD3SVARBPROC) (GLenum target, const GLshort *v);
typedef void ( * PFNGLMULTITEXCOORD4DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void ( * PFNGLMULTITEXCOORD4DVARBPROC) (GLenum target, const GLdouble *v);
typedef void ( * PFNGLMULTITEXCOORD4FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void ( * PFNGLMULTITEXCOORD4FVARBPROC) (GLenum target, const GLfloat *v);
typedef void ( * PFNGLMULTITEXCOORD4IARBPROC) (GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void ( * PFNGLMULTITEXCOORD4IVARBPROC) (GLenum target, const GLint *v);
typedef void ( * PFNGLMULTITEXCOORD4SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void ( * PFNGLMULTITEXCOORD4SVARBPROC) (GLenum target, const GLshort *v);
typedef void ( * PFNGLACTIVETEXTUREARBPROC) (GLenum target);
typedef void ( * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum target);

/*
** extension constants
*/
extern	void ( * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
extern	void ( * qglActiveTextureARB )( GLenum texture );
extern	void ( * qglClientActiveTextureARB )( GLenum texture );

extern	void ( * qglLockArraysEXT) (GLint, GLint);
extern	void ( * qglUnlockArraysEXT) (void);

//----(SA)	from Raven
extern	void ( * qglPointParameterfEXT)( GLenum, GLfloat);
extern	void ( * qglPointParameterfvEXT)( GLenum, GLfloat *);
//----(SA)	end



// S3TC compression constants
#define GL_RGB_S3TC							0x83A0
#define GL_RGB4_S3TC						0x83A1
// More, grabbed from wolf code PORT
#define	GL_COMPRESSED_RGB_S3TC_DXT1_EXT                   0x83F0
#define	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT                  0x83F1
#define	GL_COMPRESSED_RGBA_S3TC_DXT3_EXT                  0x83F2
#define	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT                  0x83F3

// And more, also from old wolf code:
// GR - update enumerants
#define GL_PN_TRIANGLES_ATI							0x87F0
#define GL_MAX_PN_TRIANGLES_TESSELATION_LEVEL_ATI	0x87F1
#define GL_PN_TRIANGLES_POINT_MODE_ATI				0x87F2
#define GL_PN_TRIANGLES_NORMAL_MODE_ATI				0x87F3
#define GL_PN_TRIANGLES_TESSELATION_LEVEL_ATI		0x87F4
#define GL_PN_TRIANGLES_POINT_MODE_LINEAR_ATI		0x87F5
#define GL_PN_TRIANGLES_POINT_MODE_CUBIC_ATI		0x87F6
#define GL_PN_TRIANGLES_NORMAL_MODE_LINEAR_ATI		0x87F7
#define GL_PN_TRIANGLES_NORMAL_MODE_QUADRATIC_ATI	0x87F8

extern	void ( * qglPNTrianglesiATI)(GLenum pname, GLint param);
extern	void ( * qglPNTrianglesfATI)(GLenum pname, GLfloat param);

#define GL_FOG_DISTANCE_MODE_NV           0x855A
#define GL_EYE_RADIAL_NV                  0x855B
#define GL_EYE_PLANE_ABSOLUTE_NV          0x855C

//===========================================================================


extern  void ( * qglAccum )(GLenum op, GLfloat value);
extern  void ( * qglAlphaFunc )(GLenum func, GLclampf ref);
extern  GLboolean ( * qglAreTexturesResident )(GLsizei n, const GLuint *textures, GLboolean *residences);
extern  void ( * qglArrayElement )(GLint i);
extern  void ( * qglBegin )(GLenum mode);
extern  void ( * qglBeginEXT )(GLenum mode, GLint verts, GLint colors, GLint normals, GLint tex0, GLint tex1);//, GLint tex2, GLint tex3);
extern  GLboolean ( * qglBeginFrame )(void);
extern  void ( * qglBeginShadow )(void);
extern  void ( * qglBindTexture )(GLenum target, GLuint texture);
extern  void ( * qglBitmap )(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
extern  void ( * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
extern  void ( * qglCallList )(GLuint list);
extern  void ( * qglCallLists )(GLsizei n, GLenum type, const GLvoid *lists);
extern  void ( * qglClear )(GLbitfield mask);
extern  void ( * qglClearAccum )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern  void ( * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
extern  void ( * qglClearDepth )(GLclampd depth);
extern  void ( * qglClearIndex )(GLfloat c);
extern  void ( * qglClearStencil )(GLint s);
extern  void ( * qglClipPlane )(GLenum plane, const GLdouble *equation);
extern  void ( * qglColor3b )(GLbyte red, GLbyte green, GLbyte blue);
extern  void ( * qglColor3bv )(const GLbyte *v);
extern  void ( * qglColor3d )(GLdouble red, GLdouble green, GLdouble blue);
extern  void ( * qglColor3dv )(const GLdouble *v);
extern  void ( * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
extern  void ( * qglColor3fv )(const GLfloat *v);
extern  void ( * qglColor3i )(GLint red, GLint green, GLint blue);
extern  void ( * qglColor3iv )(const GLint *v);
extern  void ( * qglColor3s )(GLshort red, GLshort green, GLshort blue);
extern  void ( * qglColor3sv )(const GLshort *v);
extern  void ( * qglColor3ub )(GLubyte red, GLubyte green, GLubyte blue);
extern  void ( * qglColor3ubv )(const GLubyte *v);
extern  void ( * qglColor3ui )(GLuint red, GLuint green, GLuint blue);
extern  void ( * qglColor3uiv )(const GLuint *v);
extern  void ( * qglColor3us )(GLushort red, GLushort green, GLushort blue);
extern  void ( * qglColor3usv )(const GLushort *v);
extern  void ( * qglColor4b )(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
extern  void ( * qglColor4bv )(const GLbyte *v);
extern  void ( * qglColor4d )(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
extern  void ( * qglColor4dv )(const GLdouble *v);
extern  void ( * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern  void ( * qglColor4fv )(const GLfloat *v);
extern  void ( * qglColor4i )(GLint red, GLint green, GLint blue, GLint alpha);
extern  void ( * qglColor4iv )(const GLint *v);
extern  void ( * qglColor4s )(GLshort red, GLshort green, GLshort blue, GLshort alpha);
extern  void ( * qglColor4sv )(const GLshort *v);
extern  void ( * qglColor4ub )(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
extern  void ( * qglColor4ubv )(const GLubyte *v);
extern  void ( * qglColor4ui )(GLuint red, GLuint green, GLuint blue, GLuint alpha);
extern  void ( * qglColor4uiv )(const GLuint *v);
extern  void ( * qglColor4us )(GLushort red, GLushort green, GLushort blue, GLushort alpha);
extern  void ( * qglColor4usv )(const GLushort *v);
extern  void ( * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
extern  void ( * qglColorMaterial )(GLenum face, GLenum mode);
extern  void ( * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( * qglCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
extern  void ( * qglCopyTexImage1D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border);
extern  void ( * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
extern  void ( * qglCopyTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
extern  void ( * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
extern  void ( * qglCullFace )(GLenum mode);
extern  void ( * qglDeleteLists )(GLuint list, GLsizei range);
extern  void ( * qglDeleteTextures )(GLsizei n, const GLuint *textures);
extern  void ( * qglDepthFunc )(GLenum func);
extern  void ( * qglDepthMask )(GLboolean flag);
extern  void ( * qglDepthRange )(GLclampd zNear, GLclampd zFar);
extern  void ( * qglDisable )(GLenum cap);
extern  void ( * qglDisableClientState )(GLenum array);
extern  void ( * qglDrawArrays )(GLenum mode, GLint first, GLsizei count);
extern  void ( * qglDrawBuffer )(GLenum mode);
extern  void ( * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
extern  void ( * qglDrawPixels )(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglEdgeFlag )(GLboolean flag);
extern  void ( * qglEdgeFlagPointer )(GLsizei stride, const GLvoid *pointer);
extern  void ( * qglEdgeFlagv )(const GLboolean *flag);
extern  void ( * qglEnable )(GLenum cap);
extern  void ( * qglEnableClientState )(GLenum array);
extern  void ( * qglEnd )(void);
extern  void ( * qglEndFrame )(void);
extern  void ( * qglEndShadow )(void);
extern  void ( * qglEndList )(void);
extern  void ( * qglEvalCoord1d )(GLdouble u);
extern  void ( * qglEvalCoord1dv )(const GLdouble *u);
extern  void ( * qglEvalCoord1f )(GLfloat u);
extern  void ( * qglEvalCoord1fv )(const GLfloat *u);
extern  void ( * qglEvalCoord2d )(GLdouble u, GLdouble v);
extern  void ( * qglEvalCoord2dv )(const GLdouble *u);
extern  void ( * qglEvalCoord2f )(GLfloat u, GLfloat v);
extern  void ( * qglEvalCoord2fv )(const GLfloat *u);
extern  void ( * qglEvalMesh1 )(GLenum mode, GLint i1, GLint i2);
extern  void ( * qglEvalMesh2 )(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
extern  void ( * qglEvalPoint1 )(GLint i);
extern  void ( * qglEvalPoint2 )(GLint i, GLint j);
extern  void ( * qglFeedbackBuffer )(GLsizei size, GLenum type, GLfloat *buffer);
extern  void ( * qglFinish )(void);
extern  void ( * qglFlush )(void);
extern  void ( * qglFlushShadow )(void);
extern  void ( * qglFogf )(GLenum pname, GLfloat param);
extern  void ( * qglFogfv )(GLenum pname, const GLfloat *params);
extern  void ( * qglFogi )(GLenum pname, GLint param);
extern  void ( * qglFogiv )(GLenum pname, const GLint *params);
extern  void ( * qglFrontFace )(GLenum mode);
extern  void ( * qglFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
extern  GLuint ( * qglGenLists )(GLsizei range);
extern  void ( * qglGenTextures )(GLsizei n, GLuint *textures);
extern  void ( * qglGetBooleanv )(GLenum pname, GLboolean *params);
extern  void ( * qglGetClipPlane )(GLenum plane, GLdouble *equation);
extern  void ( * qglGetDoublev )(GLenum pname, GLdouble *params);
extern  GLenum ( * qglGetError )(void);
extern  void ( * qglGetFloatv )(GLenum pname, GLfloat *params);
extern  void ( * qglGetIntegerv )(GLenum pname, GLint *params);
extern  void ( * qglGetLightfv )(GLenum light, GLenum pname, GLfloat *params);
extern  void ( * qglGetLightiv )(GLenum light, GLenum pname, GLint *params);
extern  void ( * qglGetMapdv )(GLenum target, GLenum query, GLdouble *v);
extern  void ( * qglGetMapfv )(GLenum target, GLenum query, GLfloat *v);
extern  void ( * qglGetMapiv )(GLenum target, GLenum query, GLint *v);
extern  void ( * qglGetMaterialfv )(GLenum face, GLenum pname, GLfloat *params);
extern  void ( * qglGetMaterialiv )(GLenum face, GLenum pname, GLint *params);
extern  void ( * qglGetPixelMapfv )(GLenum map, GLfloat *values);
extern  void ( * qglGetPixelMapuiv )(GLenum map, GLuint *values);
extern  void ( * qglGetPixelMapusv )(GLenum map, GLushort *values);
extern  void ( * qglGetPointerv )(GLenum pname, GLvoid* *params);
extern  void ( * qglGetPolygonStipple )(GLubyte *mask);
extern  const GLubyte * ( * qglGetString )(GLenum name);
extern  void ( * qglGetTexEnvfv )(GLenum target, GLenum pname, GLfloat *params);
extern  void ( * qglGetTexEnviv )(GLenum target, GLenum pname, GLint *params);
extern  void ( * qglGetTexGendv )(GLenum coord, GLenum pname, GLdouble *params);
extern  void ( * qglGetTexGenfv )(GLenum coord, GLenum pname, GLfloat *params);
extern  void ( * qglGetTexGeniv )(GLenum coord, GLenum pname, GLint *params);
extern  void ( * qglGetTexImage )(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
extern  void ( * qglGetTexLevelParameterfv )(GLenum target, GLint level, GLenum pname, GLfloat *params);
extern  void ( * qglGetTexLevelParameteriv )(GLenum target, GLint level, GLenum pname, GLint *params);
extern  void ( * qglGetTexParameterfv )(GLenum target, GLenum pname, GLfloat *params);
extern  void ( * qglGetTexParameteriv )(GLenum target, GLenum pname, GLint *params);
extern  void ( * qglHint )(GLenum target, GLenum mode);
extern  void ( * qglIndexedTriToStrip )(GLsizei count, const GLushort *indices);
extern  void ( * qglIndexMask )(GLuint mask);
extern  void ( * qglIndexPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( * qglIndexd )(GLdouble c);
extern  void ( * qglIndexdv )(const GLdouble *c);
extern  void ( * qglIndexf )(GLfloat c);
extern  void ( * qglIndexfv )(const GLfloat *c);
extern  void ( * qglIndexi )(GLint c);
extern  void ( * qglIndexiv )(const GLint *c);
extern  void ( * qglIndexs )(GLshort c);
extern  void ( * qglIndexsv )(const GLshort *c);
extern  void ( * qglIndexub )(GLubyte c);
extern  void ( * qglIndexubv )(const GLubyte *c);
extern  void ( * qglInitNames )(void);
extern  void ( * qglInterleavedArrays )(GLenum format, GLsizei stride, const GLvoid *pointer);
extern  GLboolean ( * qglIsEnabled )(GLenum cap);
extern  GLboolean ( * qglIsList )(GLuint listArg);
extern  GLboolean ( * qglIsTexture )(GLuint texture);
extern  void ( * qglLightModelf )(GLenum pname, GLfloat param);
extern  void ( * qglLightModelfv )(GLenum pname, const GLfloat *params);
extern  void ( * qglLightModeli )(GLenum pname, GLint param);
extern  void ( * qglLightModeliv )(GLenum pname, const GLint *params);
extern  void ( * qglLightf )(GLenum light, GLenum pname, GLfloat param);
extern  void ( * qglLightfv )(GLenum light, GLenum pname, const GLfloat *params);
extern  void ( * qglLighti )(GLenum light, GLenum pname, GLint param);
extern  void ( * qglLightiv )(GLenum light, GLenum pname, const GLint *params);
extern  void ( * qglLineStipple )(GLint factor, GLushort pattern);
extern  void ( * qglLineWidth )(GLfloat width);
extern  void ( * qglListBase )(GLuint base);
extern  void ( * qglLoadIdentity )(void);
extern  void ( * qglLoadMatrixd )(const GLdouble *m);
extern  void ( * qglLoadMatrixf )(const GLfloat *m);
extern  void ( * qglLoadName )(GLuint name);
extern  void ( * qglLogicOp )(GLenum opcode);
extern  void ( * qglMap1d )(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
extern  void ( * qglMap1f )(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
extern  void ( * qglMap2d )(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
extern  void ( * qglMap2f )(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
extern  void ( * qglMapGrid1d )(GLint un, GLdouble u1, GLdouble u2);
extern  void ( * qglMapGrid1f )(GLint un, GLfloat u1, GLfloat u2);
extern  void ( * qglMapGrid2d )(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
extern  void ( * qglMapGrid2f )(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
extern  void ( * qglMaterialf )(GLenum face, GLenum pname, GLfloat param);
extern  void ( * qglMaterialfv )(GLenum face, GLenum pname, const GLfloat *params);
extern  void ( * qglMateriali )(GLenum face, GLenum pname, GLint param);
extern  void ( * qglMaterialiv )(GLenum face, GLenum pname, const GLint *params);
extern  void ( * qglMatrixMode )(GLenum mode);
extern  void ( * qglMultMatrixd )(const GLdouble *m);
extern  void ( * qglMultMatrixf )(const GLfloat *m);
extern  void ( * qglNewList )(GLuint list, GLenum mode);
extern  void ( * qglNormal3b )(GLbyte nx, GLbyte ny, GLbyte nz);
extern  void ( * qglNormal3bv )(const GLbyte *v);
extern  void ( * qglNormal3d )(GLdouble nx, GLdouble ny, GLdouble nz);
extern  void ( * qglNormal3dv )(const GLdouble *v);
extern  void ( * qglNormal3f )(GLfloat nx, GLfloat ny, GLfloat nz);
extern  void ( * qglNormal3fv )(const GLfloat *v);
extern  void ( * qglNormal3i )(GLint nx, GLint ny, GLint nz);
extern  void ( * qglNormal3iv )(const GLint *v);
extern  void ( * qglNormal3s )(GLshort nx, GLshort ny, GLshort nz);
extern  void ( * qglNormal3sv )(const GLshort *v);
extern  void ( * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
extern  void ( * qglPassThrough )(GLfloat token);
extern  void ( * qglPixelMapfv )(GLenum map, GLsizei mapsize, const GLfloat *values);
extern  void ( * qglPixelMapuiv )(GLenum map, GLsizei mapsize, const GLuint *values);
extern  void ( * qglPixelMapusv )(GLenum map, GLsizei mapsize, const GLushort *values);
extern  void ( * qglPixelStoref )(GLenum pname, GLfloat param);
extern  void ( * qglPixelStorei )(GLenum pname, GLint param);
extern  void ( * qglPixelTransferf )(GLenum pname, GLfloat param);
extern  void ( * qglPixelTransferi )(GLenum pname, GLint param);
extern  void ( * qglPixelZoom )(GLfloat xfactor, GLfloat yfactor);
extern  void ( * qglPointSize )(GLfloat size);
extern  void ( * qglPolygonMode )(GLenum face, GLenum mode);
extern  void ( * qglPolygonOffset )(GLfloat factor, GLfloat units);
extern  void ( * qglPolygonStipple )(const GLubyte *mask);
extern  void ( * qglPopAttrib )(void);
extern  void ( * qglPopClientAttrib )(void);
extern  void ( * qglPopMatrix )(void);
extern  void ( * qglPopName )(void);
extern  void ( * qglPrioritizeTextures )(GLsizei n, const GLuint *textures, const GLclampf *priorities);
extern  void ( * qglPushAttrib )(GLbitfield mask);
extern  void ( * qglPushClientAttrib )(GLbitfield mask);
extern  void ( * qglPushMatrix )(void);
extern  void ( * qglPushName )(GLuint name);
extern  void ( * qglRasterPos2d )(GLdouble x, GLdouble y);
extern  void ( * qglRasterPos2dv )(const GLdouble *v);
extern  void ( * qglRasterPos2f )(GLfloat x, GLfloat y);
extern  void ( * qglRasterPos2fv )(const GLfloat *v);
extern  void ( * qglRasterPos2i )(GLint x, GLint y);
extern  void ( * qglRasterPos2iv )(const GLint *v);
extern  void ( * qglRasterPos2s )(GLshort x, GLshort y);
extern  void ( * qglRasterPos2sv )(const GLshort *v);
extern  void ( * qglRasterPos3d )(GLdouble x, GLdouble y, GLdouble z);
extern  void ( * qglRasterPos3dv )(const GLdouble *v);
extern  void ( * qglRasterPos3f )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( * qglRasterPos3fv )(const GLfloat *v);
extern  void ( * qglRasterPos3i )(GLint x, GLint y, GLint z);
extern  void ( * qglRasterPos3iv )(const GLint *v);
extern  void ( * qglRasterPos3s )(GLshort x, GLshort y, GLshort z);
extern  void ( * qglRasterPos3sv )(const GLshort *v);
extern  void ( * qglRasterPos4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
extern  void ( * qglRasterPos4dv )(const GLdouble *v);
extern  void ( * qglRasterPos4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
extern  void ( * qglRasterPos4fv )(const GLfloat *v);
extern  void ( * qglRasterPos4i )(GLint x, GLint y, GLint z, GLint w);
extern  void ( * qglRasterPos4iv )(const GLint *v);
extern  void ( * qglRasterPos4s )(GLshort x, GLshort y, GLshort z, GLshort w);
extern  void ( * qglRasterPos4sv )(const GLshort *v);
extern  void ( * qglReadBuffer )(GLenum mode);
extern  void ( * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei twidth, GLsizei theight, GLvoid *pixels);
extern  void ( * qglCopyBackBufferToTexEXT )(float width, float height, float u1, float v1, float u2, float v2);
extern	void ( * qglCopyBackBufferToTex )(void);
extern  void ( * qglRectd )(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
extern  void ( * qglRectdv )(const GLdouble *v1, const GLdouble *v2);
extern  void ( * qglRectf )(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
extern  void ( * qglRectfv )(const GLfloat *v1, const GLfloat *v2);
extern  void ( * qglRecti )(GLint x1, GLint y1, GLint x2, GLint y2);
extern  void ( * qglRectiv )(const GLint *v1, const GLint *v2);
extern  void ( * qglRects )(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
extern  void ( * qglRectsv )(const GLshort *v1, const GLshort *v2);
extern  GLint ( * qglRenderMode )(GLenum mode);
extern  void ( * qglRotated )(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
extern  void ( * qglRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
extern  void ( * qglScaled )(GLdouble x, GLdouble y, GLdouble z);
extern  void ( * qglScalef )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
extern  void ( * qglSelectBuffer )(GLsizei size, GLuint *buffer);
extern  void ( * qglShadeModel )(GLenum mode);
extern  void ( * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
extern  void ( * qglStencilMask )(GLuint mask);
extern  void ( * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
extern  void ( * qglTexCoord1d )(GLdouble s);
extern  void ( * qglTexCoord1dv )(const GLdouble *v);
extern  void ( * qglTexCoord1f )(GLfloat s);
extern  void ( * qglTexCoord1fv )(const GLfloat *v);
extern  void ( * qglTexCoord1i )(GLint s);
extern  void ( * qglTexCoord1iv )(const GLint *v);
extern  void ( * qglTexCoord1s )(GLshort s);
extern  void ( * qglTexCoord1sv )(const GLshort *v);
extern  void ( * qglTexCoord2d )(GLdouble s, GLdouble t);
extern  void ( * qglTexCoord2dv )(const GLdouble *v);
extern  void ( * qglTexCoord2f )(GLfloat s, GLfloat t);
extern  void ( * qglTexCoord2fv )(const GLfloat *v);
extern  void ( * qglTexCoord2i )(GLint s, GLint t);
extern  void ( * qglTexCoord2iv )(const GLint *v);
extern  void ( * qglTexCoord2s )(GLshort s, GLshort t);
extern  void ( * qglTexCoord2sv )(const GLshort *v);
extern  void ( * qglTexCoord3d )(GLdouble s, GLdouble t, GLdouble r);
extern  void ( * qglTexCoord3dv )(const GLdouble *v);
extern  void ( * qglTexCoord3f )(GLfloat s, GLfloat t, GLfloat r);
extern  void ( * qglTexCoord3fv )(const GLfloat *v);
extern  void ( * qglTexCoord3i )(GLint s, GLint t, GLint r);
extern  void ( * qglTexCoord3iv )(const GLint *v);
extern  void ( * qglTexCoord3s )(GLshort s, GLshort t, GLshort r);
extern  void ( * qglTexCoord3sv )(const GLshort *v);
extern  void ( * qglTexCoord4d )(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
extern  void ( * qglTexCoord4dv )(const GLdouble *v);
extern  void ( * qglTexCoord4f )(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
extern  void ( * qglTexCoord4fv )(const GLfloat *v);
extern  void ( * qglTexCoord4i )(GLint s, GLint t, GLint r, GLint q);
extern  void ( * qglTexCoord4iv )(const GLint *v);
extern  void ( * qglTexCoord4s )(GLshort s, GLshort t, GLshort r, GLshort q);
extern  void ( * qglTexCoord4sv )(const GLshort *v);
extern  void ( * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( * qglTexEnvf )(GLenum target, GLenum pname, GLfloat param);
extern  void ( * qglTexEnvfv )(GLenum target, GLenum pname, const GLfloat *params);
extern  void ( * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
extern  void ( * qglTexEnviv )(GLenum target, GLenum pname, const GLint *params);
extern  void ( * qglTexGend )(GLenum coord, GLenum pname, GLdouble param);
extern  void ( * qglTexGendv )(GLenum coord, GLenum pname, const GLdouble *params);
extern  void ( * qglTexGenf )(GLenum coord, GLenum pname, GLfloat param);
extern  void ( * qglTexGenfv )(GLenum coord, GLenum pname, const GLfloat *params);
extern  void ( * qglTexGeni )(GLenum coord, GLenum pname, GLint param);
extern  void ( * qglTexGeniv )(GLenum coord, GLenum pname, const GLint *params);
extern  void ( * qglTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglTexImage2DEXT )(GLenum target, GLint level, GLint numlevels, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
extern  void ( * qglTexParameterfv )(GLenum target, GLenum pname, const GLfloat *params);
extern  void ( * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
extern  void ( * qglTexParameteriv )(GLenum target, GLenum pname, const GLint *params);
extern  void ( * qglTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( * qglTranslated )(GLdouble x, GLdouble y, GLdouble z);
extern  void ( * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( * qglVertex2d )(GLdouble x, GLdouble y);
extern  void ( * qglVertex2dv )(const GLdouble *v);
extern  void ( * qglVertex2f )(GLfloat x, GLfloat y);
extern  void ( * qglVertex2fv )(const GLfloat *v);
extern  void ( * qglVertex2i )(GLint x, GLint y);
extern  void ( * qglVertex2iv )(const GLint *v);
extern  void ( * qglVertex2s )(GLshort x, GLshort y);
extern  void ( * qglVertex2sv )(const GLshort *v);
extern  void ( * qglVertex3d )(GLdouble x, GLdouble y, GLdouble z);
extern  void ( * qglVertex3dv )(const GLdouble *v);
extern  void ( * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( * qglVertex3fv )(const GLfloat *v);
extern  void ( * qglVertex3i )(GLint x, GLint y, GLint z);
extern  void ( * qglVertex3iv )(const GLint *v);
extern  void ( * qglVertex3s )(GLshort x, GLshort y, GLshort z);
extern  void ( * qglVertex3sv )(const GLshort *v);
extern  void ( * qglVertex4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
extern  void ( * qglVertex4dv )(const GLdouble *v);
extern  void ( * qglVertex4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
extern  void ( * qglVertex4fv )(const GLfloat *v);
extern  void ( * qglVertex4i )(GLint x, GLint y, GLint z, GLint w);
extern  void ( * qglVertex4iv )(const GLint *v);
extern  void ( * qglVertex4s )(GLshort x, GLshort y, GLshort z, GLshort w);
extern  void ( * qglVertex4sv )(const GLshort *v);
extern  void ( * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);


/* SP Plan-C native-DX8 bridge: this branch currently has renderer
 * call sites transformed from qglFoo(...) to glFoo(...).  Keep the
 * source call sites stable while binding them back to the native Xbox
 * QGL function pointer table implemented by win_qgl_dx8.cpp. */
#ifdef _XBOX
#ifndef glAccum
#define glAccum qglAccum
#endif
#ifndef glActiveTextureARB
#define glActiveTextureARB qglActiveTextureARB
#endif
#ifndef glAlphaFunc
#define glAlphaFunc qglAlphaFunc
#endif
#ifndef glAreTexturesResident
#define glAreTexturesResident qglAreTexturesResident
#endif
#ifndef glArrayElement
#define glArrayElement qglArrayElement
#endif
#ifndef glBegin
#define glBegin qglBegin
#endif
#ifndef glBeginEXT
#define glBeginEXT qglBeginEXT
#endif
#ifndef glBeginFrame
#define glBeginFrame qglBeginFrame
#endif
#ifndef glBeginShadow
#define glBeginShadow qglBeginShadow
#endif
#ifndef glBindTexture
#define glBindTexture qglBindTexture
#endif
#ifndef glBitmap
#define glBitmap qglBitmap
#endif
#ifndef glBlendFunc
#define glBlendFunc qglBlendFunc
#endif
#ifndef glCallList
#define glCallList qglCallList
#endif
#ifndef glCallLists
#define glCallLists qglCallLists
#endif
#ifndef glClear
#define glClear qglClear
#endif
#ifndef glClearAccum
#define glClearAccum qglClearAccum
#endif
#ifndef glClearColor
#define glClearColor qglClearColor
#endif
#ifndef glClearDepth
#define glClearDepth qglClearDepth
#endif
#ifndef glClearIndex
#define glClearIndex qglClearIndex
#endif
#ifndef glClearStencil
#define glClearStencil qglClearStencil
#endif
#ifndef glClientActiveTextureARB
#define glClientActiveTextureARB qglClientActiveTextureARB
#endif
#ifndef glClipPlane
#define glClipPlane qglClipPlane
#endif
#ifndef glColor3b
#define glColor3b qglColor3b
#endif
#ifndef glColor3bv
#define glColor3bv qglColor3bv
#endif
#ifndef glColor3d
#define glColor3d qglColor3d
#endif
#ifndef glColor3dv
#define glColor3dv qglColor3dv
#endif
#ifndef glColor3f
#define glColor3f qglColor3f
#endif
#ifndef glColor3fv
#define glColor3fv qglColor3fv
#endif
#ifndef glColor3i
#define glColor3i qglColor3i
#endif
#ifndef glColor3iv
#define glColor3iv qglColor3iv
#endif
#ifndef glColor3s
#define glColor3s qglColor3s
#endif
#ifndef glColor3sv
#define glColor3sv qglColor3sv
#endif
#ifndef glColor3ub
#define glColor3ub qglColor3ub
#endif
#ifndef glColor3ubv
#define glColor3ubv qglColor3ubv
#endif
#ifndef glColor3ui
#define glColor3ui qglColor3ui
#endif
#ifndef glColor3uiv
#define glColor3uiv qglColor3uiv
#endif
#ifndef glColor3us
#define glColor3us qglColor3us
#endif
#ifndef glColor3usv
#define glColor3usv qglColor3usv
#endif
#ifndef glColor4b
#define glColor4b qglColor4b
#endif
#ifndef glColor4bv
#define glColor4bv qglColor4bv
#endif
#ifndef glColor4d
#define glColor4d qglColor4d
#endif
#ifndef glColor4dv
#define glColor4dv qglColor4dv
#endif
#ifndef glColor4f
#define glColor4f qglColor4f
#endif
#ifndef glColor4fv
#define glColor4fv qglColor4fv
#endif
#ifndef glColor4i
#define glColor4i qglColor4i
#endif
#ifndef glColor4iv
#define glColor4iv qglColor4iv
#endif
#ifndef glColor4s
#define glColor4s qglColor4s
#endif
#ifndef glColor4sv
#define glColor4sv qglColor4sv
#endif
#ifndef glColor4ub
#define glColor4ub qglColor4ub
#endif
#ifndef glColor4ubv
#define glColor4ubv qglColor4ubv
#endif
#ifndef glColor4ui
#define glColor4ui qglColor4ui
#endif
#ifndef glColor4uiv
#define glColor4uiv qglColor4uiv
#endif
#ifndef glColor4us
#define glColor4us qglColor4us
#endif
#ifndef glColor4usv
#define glColor4usv qglColor4usv
#endif
#ifndef glColorMask
#define glColorMask qglColorMask
#endif
#ifndef glColorMaterial
#define glColorMaterial qglColorMaterial
#endif
#ifndef glColorPointer
#define glColorPointer qglColorPointer
#endif
#ifndef glCopyBackBufferToTex
#define glCopyBackBufferToTex qglCopyBackBufferToTex
#endif
#ifndef glCopyBackBufferToTexEXT
#define glCopyBackBufferToTexEXT qglCopyBackBufferToTexEXT
#endif
#ifndef glCopyPixels
#define glCopyPixels qglCopyPixels
#endif
#ifndef glCopyTexImage1D
#define glCopyTexImage1D qglCopyTexImage1D
#endif
#ifndef glCopyTexImage2D
#define glCopyTexImage2D qglCopyTexImage2D
#endif
#ifndef glCopyTexSubImage1D
#define glCopyTexSubImage1D qglCopyTexSubImage1D
#endif
#ifndef glCopyTexSubImage2D
#define glCopyTexSubImage2D qglCopyTexSubImage2D
#endif
#ifndef glCullFace
#define glCullFace qglCullFace
#endif
#ifndef glDeleteLists
#define glDeleteLists qglDeleteLists
#endif
#ifndef glDeleteTextures
#define glDeleteTextures qglDeleteTextures
#endif
#ifndef glDepthFunc
#define glDepthFunc qglDepthFunc
#endif
#ifndef glDepthMask
#define glDepthMask qglDepthMask
#endif
#ifndef glDepthRange
#define glDepthRange qglDepthRange
#endif
#ifndef glDisable
#define glDisable qglDisable
#endif
#ifndef glDisableClientState
#define glDisableClientState qglDisableClientState
#endif
#ifndef glDrawArrays
#define glDrawArrays qglDrawArrays
#endif
#ifndef glDrawBuffer
#define glDrawBuffer qglDrawBuffer
#endif
#ifndef glDrawElements
#define glDrawElements qglDrawElements
#endif
#ifndef glDrawPixels
#define glDrawPixels qglDrawPixels
#endif
#ifndef glEdgeFlag
#define glEdgeFlag qglEdgeFlag
#endif
#ifndef glEdgeFlagPointer
#define glEdgeFlagPointer qglEdgeFlagPointer
#endif
#ifndef glEdgeFlagv
#define glEdgeFlagv qglEdgeFlagv
#endif
#ifndef glEnable
#define glEnable qglEnable
#endif
#ifndef glEnableClientState
#define glEnableClientState qglEnableClientState
#endif
#ifndef glEnd
#define glEnd qglEnd
#endif
#ifndef glEndFrame
#define glEndFrame qglEndFrame
#endif
#ifndef glEndList
#define glEndList qglEndList
#endif
#ifndef glEndShadow
#define glEndShadow qglEndShadow
#endif
#ifndef glEvalCoord1d
#define glEvalCoord1d qglEvalCoord1d
#endif
#ifndef glEvalCoord1dv
#define glEvalCoord1dv qglEvalCoord1dv
#endif
#ifndef glEvalCoord1f
#define glEvalCoord1f qglEvalCoord1f
#endif
#ifndef glEvalCoord1fv
#define glEvalCoord1fv qglEvalCoord1fv
#endif
#ifndef glEvalCoord2d
#define glEvalCoord2d qglEvalCoord2d
#endif
#ifndef glEvalCoord2dv
#define glEvalCoord2dv qglEvalCoord2dv
#endif
#ifndef glEvalCoord2f
#define glEvalCoord2f qglEvalCoord2f
#endif
#ifndef glEvalCoord2fv
#define glEvalCoord2fv qglEvalCoord2fv
#endif
#ifndef glEvalMesh1
#define glEvalMesh1 qglEvalMesh1
#endif
#ifndef glEvalMesh2
#define glEvalMesh2 qglEvalMesh2
#endif
#ifndef glEvalPoint1
#define glEvalPoint1 qglEvalPoint1
#endif
#ifndef glEvalPoint2
#define glEvalPoint2 qglEvalPoint2
#endif
#ifndef glFeedbackBuffer
#define glFeedbackBuffer qglFeedbackBuffer
#endif
#ifndef glFinish
#define glFinish qglFinish
#endif
#ifndef glFlush
#define glFlush qglFlush
#endif
#ifndef glFlushShadow
#define glFlushShadow qglFlushShadow
#endif
#ifndef glFogf
#define glFogf qglFogf
#endif
#ifndef glFogfv
#define glFogfv qglFogfv
#endif
#ifndef glFogi
#define glFogi qglFogi
#endif
#ifndef glFogiv
#define glFogiv qglFogiv
#endif
#ifndef glFrontFace
#define glFrontFace qglFrontFace
#endif
#ifndef glFrustum
#define glFrustum qglFrustum
#endif
#ifndef glGenLists
#define glGenLists qglGenLists
#endif
#ifndef glGenTextures
#define glGenTextures qglGenTextures
#endif
#ifndef glGetBooleanv
#define glGetBooleanv qglGetBooleanv
#endif
#ifndef glGetClipPlane
#define glGetClipPlane qglGetClipPlane
#endif
#ifndef glGetDoublev
#define glGetDoublev qglGetDoublev
#endif
#ifndef glGetError
#define glGetError qglGetError
#endif
#ifndef glGetFloatv
#define glGetFloatv qglGetFloatv
#endif
#ifndef glGetIntegerv
#define glGetIntegerv qglGetIntegerv
#endif
#ifndef glGetLightfv
#define glGetLightfv qglGetLightfv
#endif
#ifndef glGetLightiv
#define glGetLightiv qglGetLightiv
#endif
#ifndef glGetMapdv
#define glGetMapdv qglGetMapdv
#endif
#ifndef glGetMapfv
#define glGetMapfv qglGetMapfv
#endif
#ifndef glGetMapiv
#define glGetMapiv qglGetMapiv
#endif
#ifndef glGetMaterialfv
#define glGetMaterialfv qglGetMaterialfv
#endif
#ifndef glGetMaterialiv
#define glGetMaterialiv qglGetMaterialiv
#endif
#ifndef glGetPixelMapfv
#define glGetPixelMapfv qglGetPixelMapfv
#endif
#ifndef glGetPixelMapuiv
#define glGetPixelMapuiv qglGetPixelMapuiv
#endif
#ifndef glGetPixelMapusv
#define glGetPixelMapusv qglGetPixelMapusv
#endif
#ifndef glGetPointerv
#define glGetPointerv qglGetPointerv
#endif
#ifndef glGetPolygonStipple
#define glGetPolygonStipple qglGetPolygonStipple
#endif
#ifndef glGetString
#define glGetString qglGetString
#endif
#ifndef glGetTexEnvfv
#define glGetTexEnvfv qglGetTexEnvfv
#endif
#ifndef glGetTexEnviv
#define glGetTexEnviv qglGetTexEnviv
#endif
#ifndef glGetTexGendv
#define glGetTexGendv qglGetTexGendv
#endif
#ifndef glGetTexGenfv
#define glGetTexGenfv qglGetTexGenfv
#endif
#ifndef glGetTexGeniv
#define glGetTexGeniv qglGetTexGeniv
#endif
#ifndef glGetTexImage
#define glGetTexImage qglGetTexImage
#endif
#ifndef glGetTexLevelParameterfv
#define glGetTexLevelParameterfv qglGetTexLevelParameterfv
#endif
#ifndef glGetTexLevelParameteriv
#define glGetTexLevelParameteriv qglGetTexLevelParameteriv
#endif
#ifndef glGetTexParameterfv
#define glGetTexParameterfv qglGetTexParameterfv
#endif
#ifndef glGetTexParameteriv
#define glGetTexParameteriv qglGetTexParameteriv
#endif
#ifndef glHint
#define glHint qglHint
#endif
#ifndef glIndexd
#define glIndexd qglIndexd
#endif
#ifndef glIndexdv
#define glIndexdv qglIndexdv
#endif
#ifndef glIndexedTriToStrip
#define glIndexedTriToStrip qglIndexedTriToStrip
#endif
#ifndef glIndexf
#define glIndexf qglIndexf
#endif
#ifndef glIndexfv
#define glIndexfv qglIndexfv
#endif
#ifndef glIndexi
#define glIndexi qglIndexi
#endif
#ifndef glIndexiv
#define glIndexiv qglIndexiv
#endif
#ifndef glIndexMask
#define glIndexMask qglIndexMask
#endif
#ifndef glIndexPointer
#define glIndexPointer qglIndexPointer
#endif
#ifndef glIndexs
#define glIndexs qglIndexs
#endif
#ifndef glIndexsv
#define glIndexsv qglIndexsv
#endif
#ifndef glIndexub
#define glIndexub qglIndexub
#endif
#ifndef glIndexubv
#define glIndexubv qglIndexubv
#endif
#ifndef glInitNames
#define glInitNames qglInitNames
#endif
#ifndef glInterleavedArrays
#define glInterleavedArrays qglInterleavedArrays
#endif
#ifndef glIsEnabled
#define glIsEnabled qglIsEnabled
#endif
#ifndef glIsList
#define glIsList qglIsList
#endif
#ifndef glIsTexture
#define glIsTexture qglIsTexture
#endif
#ifndef glLightf
#define glLightf qglLightf
#endif
#ifndef glLightfv
#define glLightfv qglLightfv
#endif
#ifndef glLighti
#define glLighti qglLighti
#endif
#ifndef glLightiv
#define glLightiv qglLightiv
#endif
#ifndef glLightModelf
#define glLightModelf qglLightModelf
#endif
#ifndef glLightModelfv
#define glLightModelfv qglLightModelfv
#endif
#ifndef glLightModeli
#define glLightModeli qglLightModeli
#endif
#ifndef glLightModeliv
#define glLightModeliv qglLightModeliv
#endif
#ifndef glLineStipple
#define glLineStipple qglLineStipple
#endif
#ifndef glLineWidth
#define glLineWidth qglLineWidth
#endif
#ifndef glListBase
#define glListBase qglListBase
#endif
#ifndef glLoadIdentity
#define glLoadIdentity qglLoadIdentity
#endif
#ifndef glLoadMatrixd
#define glLoadMatrixd qglLoadMatrixd
#endif
#ifndef glLoadMatrixf
#define glLoadMatrixf qglLoadMatrixf
#endif
#ifndef glLoadName
#define glLoadName qglLoadName
#endif
#ifndef glLockArraysEXT
#define glLockArraysEXT qglLockArraysEXT
#endif
#ifndef glLogicOp
#define glLogicOp qglLogicOp
#endif
#ifndef glMap1d
#define glMap1d qglMap1d
#endif
#ifndef glMap1f
#define glMap1f qglMap1f
#endif
#ifndef glMap2d
#define glMap2d qglMap2d
#endif
#ifndef glMap2f
#define glMap2f qglMap2f
#endif
#ifndef glMapGrid1d
#define glMapGrid1d qglMapGrid1d
#endif
#ifndef glMapGrid1f
#define glMapGrid1f qglMapGrid1f
#endif
#ifndef glMapGrid2d
#define glMapGrid2d qglMapGrid2d
#endif
#ifndef glMapGrid2f
#define glMapGrid2f qglMapGrid2f
#endif
#ifndef glMaterialf
#define glMaterialf qglMaterialf
#endif
#ifndef glMaterialfv
#define glMaterialfv qglMaterialfv
#endif
#ifndef glMateriali
#define glMateriali qglMateriali
#endif
#ifndef glMaterialiv
#define glMaterialiv qglMaterialiv
#endif
#ifndef glMatrixMode
#define glMatrixMode qglMatrixMode
#endif
#ifndef glMultiTexCoord2fARB
#define glMultiTexCoord2fARB qglMultiTexCoord2fARB
#endif
#ifndef glMultMatrixd
#define glMultMatrixd qglMultMatrixd
#endif
#ifndef glMultMatrixf
#define glMultMatrixf qglMultMatrixf
#endif
#ifndef glNewList
#define glNewList qglNewList
#endif
#ifndef glNormal3b
#define glNormal3b qglNormal3b
#endif
#ifndef glNormal3bv
#define glNormal3bv qglNormal3bv
#endif
#ifndef glNormal3d
#define glNormal3d qglNormal3d
#endif
#ifndef glNormal3dv
#define glNormal3dv qglNormal3dv
#endif
#ifndef glNormal3f
#define glNormal3f qglNormal3f
#endif
#ifndef glNormal3fv
#define glNormal3fv qglNormal3fv
#endif
#ifndef glNormal3i
#define glNormal3i qglNormal3i
#endif
#ifndef glNormal3iv
#define glNormal3iv qglNormal3iv
#endif
#ifndef glNormal3s
#define glNormal3s qglNormal3s
#endif
#ifndef glNormal3sv
#define glNormal3sv qglNormal3sv
#endif
#ifndef glNormalPointer
#define glNormalPointer qglNormalPointer
#endif
#ifndef glOrtho
#define glOrtho qglOrtho
#endif
#ifndef glPassThrough
#define glPassThrough qglPassThrough
#endif
#ifndef glPixelMapfv
#define glPixelMapfv qglPixelMapfv
#endif
#ifndef glPixelMapuiv
#define glPixelMapuiv qglPixelMapuiv
#endif
#ifndef glPixelMapusv
#define glPixelMapusv qglPixelMapusv
#endif
#ifndef glPixelStoref
#define glPixelStoref qglPixelStoref
#endif
#ifndef glPixelStorei
#define glPixelStorei qglPixelStorei
#endif
#ifndef glPixelTransferf
#define glPixelTransferf qglPixelTransferf
#endif
#ifndef glPixelTransferi
#define glPixelTransferi qglPixelTransferi
#endif
#ifndef glPixelZoom
#define glPixelZoom qglPixelZoom
#endif
#ifndef glPNTrianglesfATI
#define glPNTrianglesfATI qglPNTrianglesfATI
#endif
#ifndef glPNTrianglesiATI
#define glPNTrianglesiATI qglPNTrianglesiATI
#endif
#ifndef glPointParameterfEXT
#define glPointParameterfEXT qglPointParameterfEXT
#endif
#ifndef glPointParameterfvEXT
#define glPointParameterfvEXT qglPointParameterfvEXT
#endif
#ifndef glPointSize
#define glPointSize qglPointSize
#endif
#ifndef glPolygonMode
#define glPolygonMode qglPolygonMode
#endif
#ifndef glPolygonOffset
#define glPolygonOffset qglPolygonOffset
#endif
#ifndef glPolygonStipple
#define glPolygonStipple qglPolygonStipple
#endif
#ifndef glPopAttrib
#define glPopAttrib qglPopAttrib
#endif
#ifndef glPopClientAttrib
#define glPopClientAttrib qglPopClientAttrib
#endif
#ifndef glPopMatrix
#define glPopMatrix qglPopMatrix
#endif
#ifndef glPopName
#define glPopName qglPopName
#endif
#ifndef glPrioritizeTextures
#define glPrioritizeTextures qglPrioritizeTextures
#endif
#ifndef glPushAttrib
#define glPushAttrib qglPushAttrib
#endif
#ifndef glPushClientAttrib
#define glPushClientAttrib qglPushClientAttrib
#endif
#ifndef glPushMatrix
#define glPushMatrix qglPushMatrix
#endif
#ifndef glPushName
#define glPushName qglPushName
#endif
#ifndef glRasterPos2d
#define glRasterPos2d qglRasterPos2d
#endif
#ifndef glRasterPos2dv
#define glRasterPos2dv qglRasterPos2dv
#endif
#ifndef glRasterPos2f
#define glRasterPos2f qglRasterPos2f
#endif
#ifndef glRasterPos2fv
#define glRasterPos2fv qglRasterPos2fv
#endif
#ifndef glRasterPos2i
#define glRasterPos2i qglRasterPos2i
#endif
#ifndef glRasterPos2iv
#define glRasterPos2iv qglRasterPos2iv
#endif
#ifndef glRasterPos2s
#define glRasterPos2s qglRasterPos2s
#endif
#ifndef glRasterPos2sv
#define glRasterPos2sv qglRasterPos2sv
#endif
#ifndef glRasterPos3d
#define glRasterPos3d qglRasterPos3d
#endif
#ifndef glRasterPos3dv
#define glRasterPos3dv qglRasterPos3dv
#endif
#ifndef glRasterPos3f
#define glRasterPos3f qglRasterPos3f
#endif
#ifndef glRasterPos3fv
#define glRasterPos3fv qglRasterPos3fv
#endif
#ifndef glRasterPos3i
#define glRasterPos3i qglRasterPos3i
#endif
#ifndef glRasterPos3iv
#define glRasterPos3iv qglRasterPos3iv
#endif
#ifndef glRasterPos3s
#define glRasterPos3s qglRasterPos3s
#endif
#ifndef glRasterPos3sv
#define glRasterPos3sv qglRasterPos3sv
#endif
#ifndef glRasterPos4d
#define glRasterPos4d qglRasterPos4d
#endif
#ifndef glRasterPos4dv
#define glRasterPos4dv qglRasterPos4dv
#endif
#ifndef glRasterPos4f
#define glRasterPos4f qglRasterPos4f
#endif
#ifndef glRasterPos4fv
#define glRasterPos4fv qglRasterPos4fv
#endif
#ifndef glRasterPos4i
#define glRasterPos4i qglRasterPos4i
#endif
#ifndef glRasterPos4iv
#define glRasterPos4iv qglRasterPos4iv
#endif
#ifndef glRasterPos4s
#define glRasterPos4s qglRasterPos4s
#endif
#ifndef glRasterPos4sv
#define glRasterPos4sv qglRasterPos4sv
#endif
#ifndef glReadBuffer
#define glReadBuffer qglReadBuffer
#endif
#ifndef glReadPixels
#define glReadPixels qglReadPixels
#endif
#ifndef glRectd
#define glRectd qglRectd
#endif
#ifndef glRectdv
#define glRectdv qglRectdv
#endif
#ifndef glRectf
#define glRectf qglRectf
#endif
#ifndef glRectfv
#define glRectfv qglRectfv
#endif
#ifndef glRecti
#define glRecti qglRecti
#endif
#ifndef glRectiv
#define glRectiv qglRectiv
#endif
#ifndef glRects
#define glRects qglRects
#endif
#ifndef glRectsv
#define glRectsv qglRectsv
#endif
#ifndef glRenderMode
#define glRenderMode qglRenderMode
#endif
#ifndef glRotated
#define glRotated qglRotated
#endif
#ifndef glRotatef
#define glRotatef qglRotatef
#endif
#ifndef glScaled
#define glScaled qglScaled
#endif
#ifndef glScalef
#define glScalef qglScalef
#endif
#ifndef glScissor
#define glScissor qglScissor
#endif
#ifndef glSelectBuffer
#define glSelectBuffer qglSelectBuffer
#endif
#ifndef glShadeModel
#define glShadeModel qglShadeModel
#endif
#ifndef glStencilFunc
#define glStencilFunc qglStencilFunc
#endif
#ifndef glStencilMask
#define glStencilMask qglStencilMask
#endif
#ifndef glStencilOp
#define glStencilOp qglStencilOp
#endif
#ifndef glTexCoord1d
#define glTexCoord1d qglTexCoord1d
#endif
#ifndef glTexCoord1dv
#define glTexCoord1dv qglTexCoord1dv
#endif
#ifndef glTexCoord1f
#define glTexCoord1f qglTexCoord1f
#endif
#ifndef glTexCoord1fv
#define glTexCoord1fv qglTexCoord1fv
#endif
#ifndef glTexCoord1i
#define glTexCoord1i qglTexCoord1i
#endif
#ifndef glTexCoord1iv
#define glTexCoord1iv qglTexCoord1iv
#endif
#ifndef glTexCoord1s
#define glTexCoord1s qglTexCoord1s
#endif
#ifndef glTexCoord1sv
#define glTexCoord1sv qglTexCoord1sv
#endif
#ifndef glTexCoord2d
#define glTexCoord2d qglTexCoord2d
#endif
#ifndef glTexCoord2dv
#define glTexCoord2dv qglTexCoord2dv
#endif
#ifndef glTexCoord2f
#define glTexCoord2f qglTexCoord2f
#endif
#ifndef glTexCoord2fv
#define glTexCoord2fv qglTexCoord2fv
#endif
#ifndef glTexCoord2i
#define glTexCoord2i qglTexCoord2i
#endif
#ifndef glTexCoord2iv
#define glTexCoord2iv qglTexCoord2iv
#endif
#ifndef glTexCoord2s
#define glTexCoord2s qglTexCoord2s
#endif
#ifndef glTexCoord2sv
#define glTexCoord2sv qglTexCoord2sv
#endif
#ifndef glTexCoord3d
#define glTexCoord3d qglTexCoord3d
#endif
#ifndef glTexCoord3dv
#define glTexCoord3dv qglTexCoord3dv
#endif
#ifndef glTexCoord3f
#define glTexCoord3f qglTexCoord3f
#endif
#ifndef glTexCoord3fv
#define glTexCoord3fv qglTexCoord3fv
#endif
#ifndef glTexCoord3i
#define glTexCoord3i qglTexCoord3i
#endif
#ifndef glTexCoord3iv
#define glTexCoord3iv qglTexCoord3iv
#endif
#ifndef glTexCoord3s
#define glTexCoord3s qglTexCoord3s
#endif
#ifndef glTexCoord3sv
#define glTexCoord3sv qglTexCoord3sv
#endif
#ifndef glTexCoord4d
#define glTexCoord4d qglTexCoord4d
#endif
#ifndef glTexCoord4dv
#define glTexCoord4dv qglTexCoord4dv
#endif
#ifndef glTexCoord4f
#define glTexCoord4f qglTexCoord4f
#endif
#ifndef glTexCoord4fv
#define glTexCoord4fv qglTexCoord4fv
#endif
#ifndef glTexCoord4i
#define glTexCoord4i qglTexCoord4i
#endif
#ifndef glTexCoord4iv
#define glTexCoord4iv qglTexCoord4iv
#endif
#ifndef glTexCoord4s
#define glTexCoord4s qglTexCoord4s
#endif
#ifndef glTexCoord4sv
#define glTexCoord4sv qglTexCoord4sv
#endif
#ifndef glTexCoordPointer
#define glTexCoordPointer qglTexCoordPointer
#endif
#ifndef glTexEnvf
#define glTexEnvf qglTexEnvf
#endif
#ifndef glTexEnvfv
#define glTexEnvfv qglTexEnvfv
#endif
#ifndef glTexEnvi
#define glTexEnvi qglTexEnvi
#endif
#ifndef glTexEnviv
#define glTexEnviv qglTexEnviv
#endif
#ifndef glTexGend
#define glTexGend qglTexGend
#endif
#ifndef glTexGendv
#define glTexGendv qglTexGendv
#endif
#ifndef glTexGenf
#define glTexGenf qglTexGenf
#endif
#ifndef glTexGenfv
#define glTexGenfv qglTexGenfv
#endif
#ifndef glTexGeni
#define glTexGeni qglTexGeni
#endif
#ifndef glTexGeniv
#define glTexGeniv qglTexGeniv
#endif
#ifndef glTexImage1D
#define glTexImage1D qglTexImage1D
#endif
#ifndef glTexImage2D
#define glTexImage2D qglTexImage2D
#endif
#ifndef glTexImage2DEXT
#define glTexImage2DEXT qglTexImage2DEXT
#endif
#ifndef glTexParameterf
#define glTexParameterf qglTexParameterf
#endif
#ifndef glTexParameterfv
#define glTexParameterfv qglTexParameterfv
#endif
#ifndef glTexParameteri
#define glTexParameteri qglTexParameteri
#endif
#ifndef glTexParameteriv
#define glTexParameteriv qglTexParameteriv
#endif
#ifndef glTexSubImage1D
#define glTexSubImage1D qglTexSubImage1D
#endif
#ifndef glTexSubImage2D
#define glTexSubImage2D qglTexSubImage2D
#endif
#ifndef glTranslated
#define glTranslated qglTranslated
#endif
#ifndef glTranslatef
#define glTranslatef qglTranslatef
#endif
#ifndef glUnlockArraysEXT
#define glUnlockArraysEXT qglUnlockArraysEXT
#endif
#ifndef glVertex2d
#define glVertex2d qglVertex2d
#endif
#ifndef glVertex2dv
#define glVertex2dv qglVertex2dv
#endif
#ifndef glVertex2f
#define glVertex2f qglVertex2f
#endif
#ifndef glVertex2fv
#define glVertex2fv qglVertex2fv
#endif
#ifndef glVertex2i
#define glVertex2i qglVertex2i
#endif
#ifndef glVertex2iv
#define glVertex2iv qglVertex2iv
#endif
#ifndef glVertex2s
#define glVertex2s qglVertex2s
#endif
#ifndef glVertex2sv
#define glVertex2sv qglVertex2sv
#endif
#ifndef glVertex3d
#define glVertex3d qglVertex3d
#endif
#ifndef glVertex3dv
#define glVertex3dv qglVertex3dv
#endif
#ifndef glVertex3f
#define glVertex3f qglVertex3f
#endif
#ifndef glVertex3fv
#define glVertex3fv qglVertex3fv
#endif
#ifndef glVertex3i
#define glVertex3i qglVertex3i
#endif
#ifndef glVertex3iv
#define glVertex3iv qglVertex3iv
#endif
#ifndef glVertex3s
#define glVertex3s qglVertex3s
#endif
#ifndef glVertex3sv
#define glVertex3sv qglVertex3sv
#endif
#ifndef glVertex4d
#define glVertex4d qglVertex4d
#endif
#ifndef glVertex4dv
#define glVertex4dv qglVertex4dv
#endif
#ifndef glVertex4f
#define glVertex4f qglVertex4f
#endif
#ifndef glVertex4fv
#define glVertex4fv qglVertex4fv
#endif
#ifndef glVertex4i
#define glVertex4i qglVertex4i
#endif
#ifndef glVertex4iv
#define glVertex4iv qglVertex4iv
#endif
#ifndef glVertex4s
#define glVertex4s qglVertex4s
#endif
#ifndef glVertex4sv
#define glVertex4sv qglVertex4sv
#endif
#ifndef glVertexPointer
#define glVertexPointer qglVertexPointer
#endif
#ifndef glViewport
#define glViewport qglViewport
#endif
#endif
#endif
