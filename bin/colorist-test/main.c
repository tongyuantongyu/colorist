// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2018.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#include "colorist/colorist.h"

#include "colorist/transform.h"

static void setFloat4(float c[4], float v0, float v1, float v2, float v3) { c[0] = v0; c[1] = v1; c[2] = v2; c[3] = v3; }
static void setFloat3(float c[3], float v0, float v1, float v2, float v3) { c[0] = v0; c[1] = v1; c[2] = v2; }
static void setRGBA8_4(uint8_t c[4], uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) { c[0] = v0; c[1] = v1; c[2] = v2; c[3] = v3; }
static void setRGBA8_3(uint8_t c[3], uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) { c[0] = v0; c[1] = v1; c[2] = v2; }
static void setRGBA16_4(uint16_t c[4], uint16_t v0, uint16_t v1, uint16_t v2, uint16_t v3) { c[0] = v0; c[1] = v1; c[2] = v2; c[3] = v3; }
static void setRGBA16_3(uint16_t c[3], uint16_t v0, uint16_t v1, uint16_t v2, uint16_t v3) { c[0] = v0; c[1] = v1; c[2] = v2; }

int main(int argc, char * argv[])
{
    clContext * C = clContextCreate(NULL);
    clConversionParams params;
    clProfilePrimaries primaries;
    clProfileCurve curve;
    clTransform * transform;
    struct clProfile * srcProfile;
    struct clProfile * dstProfile;
    clImage * srcImage;
    clImage * dstImage;

    // Basic clImageDebugDump test
    {
        C = clContextCreate(NULL);

        srcImage = clImageParseString(C, "8x8,(255,0,0)", 8, NULL);
        clImageDebugDump(C, srcImage, 0, 0, 1, 1, 0);

        clImageDestroy(C, srcImage);
        clContextDestroy(C);
    }

    // Test all CCMM RGBA reformat paths
    {
        C = clContextCreate(NULL);
        clConversionParamsSetDefaults(C, &params);
        params.jobs = 1;

        // RGBA8 -> RGBA8
        srcImage = clImageParseString(C, "8x8,(255,0,0)", 8, NULL);
        dstImage = clImageConvert(C, srcImage, &params);
        clImageDestroy(C, srcImage);
        clImageDestroy(C, dstImage);

        // RGBA16 -> RGBA16
        srcImage = clImageParseString(C, "8x8,(255,0,0)", 16, NULL);
        dstImage = clImageConvert(C, srcImage, &params);
        clImageDestroy(C, srcImage);
        clImageDestroy(C, dstImage);

        // RGBA16 -> RGBA8
        params.bpp = 8;
        srcImage = clImageParseString(C, "8x8,(255,0,0)", 16, NULL);
        dstImage = clImageConvert(C, srcImage, &params);
        clImageDestroy(C, srcImage);
        clImageDestroy(C, dstImage);

        // RGBA8 -> RGBA16
        params.bpp = 16;
        srcImage = clImageParseString(C, "8x8,(255,0,0)", 8, NULL);
        dstImage = clImageConvert(C, srcImage, &params);
        clImageDestroy(C, srcImage);
        clImageDestroy(C, dstImage);

        clContextDestroy(C);
    }

    // Directly test RGB(A) -> RGB(A) transforms
    {
        uint8_t srcRGBA8[4];
        uint8_t dstRGBA8[4];
        uint16_t srcRGBA16[4];
        uint16_t dstRGBA16[4];
        C = clContextCreate(NULL);
        srcProfile = clProfileCreateStock(C, CL_PS_SRGB);

        // RGB8 -> RGBA8
        setRGBA8_4(srcRGBA8, 255, 0, 0, 0);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_8, srcProfile, CL_TF_RGBA_8);
        clTransformRun(C, transform, 1, srcRGBA8, dstRGBA8, 1);
        printf("RGB8(%u, %u, %u) -> RGBA8(%u, %u, %u, %u)\n", srcRGBA8[0], srcRGBA8[1], srcRGBA8[2], dstRGBA8[0], dstRGBA8[1], dstRGBA8[2], dstRGBA8[3]);
        clTransformDestroy(C, transform);

        // RGBA8 -> RGB8
        setRGBA8_4(srcRGBA8, 255, 0, 0, 255);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_8, srcProfile, CL_TF_RGBA_8);
        clTransformRun(C, transform, 1, srcRGBA8, dstRGBA8, 1);
        printf("RGB8(%u, %u, %u, %u) -> RGBA8(%u, %u, %u)\n", srcRGBA8[0], srcRGBA8[1], srcRGBA8[2], srcRGBA8[3], dstRGBA8[0], dstRGBA8[1], dstRGBA8[2]);
        clTransformDestroy(C, transform);

        // RGB16 -> RGBA16
        setRGBA16_4(srcRGBA16, 255, 0, 0, 0);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_16, srcProfile, CL_TF_RGBA_16);
        clTransformRun(C, transform, 1, srcRGBA16, dstRGBA16, 1);
        printf("RGB16(%u, %u, %u) -> RGBA16(%u, %u, %u, %u)\n", srcRGBA16[0], srcRGBA16[1], srcRGBA16[2], dstRGBA16[0], dstRGBA16[1], dstRGBA16[2], dstRGBA16[3]);
        clTransformDestroy(C, transform);

        // RGBA16 -> RGB16
        setRGBA16_4(srcRGBA16, 255, 0, 0, 255);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_16, srcProfile, CL_TF_RGBA_16);
        clTransformRun(C, transform, 1, srcRGBA16, dstRGBA16, 1);
        printf("RGB16(%u, %u, %u, %u) -> RGBA16(%u, %u, %u)\n", srcRGBA16[0], srcRGBA16[1], srcRGBA16[2], srcRGBA16[3], dstRGBA16[0], dstRGBA16[1], dstRGBA16[2]);
        clTransformDestroy(C, transform);

        // RGB8 -> RGBA16
        setRGBA8_4(srcRGBA8, 255, 0, 0, 0);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_8, srcProfile, CL_TF_RGBA_16);
        clTransformRun(C, transform, 1, srcRGBA8, dstRGBA16, 1);
        printf("RGB8(%u, %u, %u) -> RGBA16(%u, %u, %u, %u)\n", srcRGBA8[0], srcRGBA8[1], srcRGBA8[2], dstRGBA16[0], dstRGBA16[1], dstRGBA16[2], dstRGBA16[3]);
        clTransformDestroy(C, transform);

        // RGB16 -> RGBA8
        setRGBA16_4(srcRGBA16, 65535, 0, 0, 0);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_16, srcProfile, CL_TF_RGBA_8);
        clTransformRun(C, transform, 1, srcRGBA16, dstRGBA8, 1);
        printf("RGB16(%u, %u, %u) -> RGBA8(%u, %u, %u, %u)\n", srcRGBA16[0], srcRGBA16[1], srcRGBA16[2], dstRGBA8[0], dstRGBA8[1], dstRGBA8[2], dstRGBA8[3]);
        clTransformDestroy(C, transform);

        clProfileDestroy(C, srcProfile);
        clContextDestroy(C);
    }

    // Directly test floating point transforms
    {
        float srcRGBA[4];
        float dstRGBA[4];
        float xyz[3];
        C = clContextCreate(NULL);
        srcProfile = clProfileCreateStock(C, CL_PS_SRGB);
        clContextGetStockPrimaries(C, "bt2020", &primaries);
        curve.type = CL_PCT_GAMMA;
        curve.gamma = 2.2f;
        dstProfile = clProfileCreate(C, &primaries, &curve, 10000, NULL);

        // sRGBA -> XYZ
        setFloat4(srcRGBA, 1.0f, 0.0f, 0.0f, 1.0f);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGBA_FLOAT, NULL, CL_TF_XYZ_FLOAT);
        clTransformRun(C, transform, 1, srcRGBA, xyz, 1);
        printf("sRGBA(%g,%g,%g,%g) -> XYZ(%g,%g,%g)\n", srcRGBA[0], srcRGBA[1], srcRGBA[2], srcRGBA[3], xyz[0], xyz[1], xyz[2]);
        clTransformDestroy(C, transform);

        // sRGBA -> BT.2020 10k nits G2.2
        setFloat4(srcRGBA, 1.0f, 0.0f, 0.0f, 1.0f);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGBA_FLOAT, dstProfile, CL_TF_RGBA_FLOAT);
        clTransformRun(C, transform, 1, srcRGBA, dstRGBA, 1);
        printf("sRGBA(%g,%g,%g,%g) -> BT2020_10k_G22(%g,%g,%g,%g)\n", srcRGBA[0], srcRGBA[1], srcRGBA[2], srcRGBA[3], dstRGBA[0], dstRGBA[1], dstRGBA[2], dstRGBA[3]);
        clTransformDestroy(C, transform);

        // sRGBA -> sRGB
        setFloat4(srcRGBA, 1.0f, 0.0f, 0.0f, 1.0f);
        setFloat4(dstRGBA, 0.0f, 0.0f, 0.0f, 0.0f);
        transform = clTransformCreate(C, srcProfile, CL_TF_RGBA_FLOAT, srcProfile, CL_TF_RGB_FLOAT);
        clTransformRun(C, transform, 1, srcRGBA, dstRGBA, 1);
        printf("sRGBA(%g,%g,%g) -> sRGB(%g,%g,%g) (%g == 0)\n", srcRGBA[0], srcRGBA[1], srcRGBA[2], dstRGBA[0], dstRGBA[1], dstRGBA[2], dstRGBA[3]);
        clTransformDestroy(C, transform);

        // sRGB -> sRGBA
        setFloat4(srcRGBA, 1.0f, 0.0f, 0.0f, 0.0f); // set alpha to 0 to prove it doesn't carry over
        transform = clTransformCreate(C, srcProfile, CL_TF_RGB_FLOAT, srcProfile, CL_TF_RGBA_FLOAT);
        clTransformRun(C, transform, 1, srcRGBA, dstRGBA, 1);
        printf("sRGB(%g,%g,%g) -> sRGBA(%g,%g,%g,%g)\n", srcRGBA[0], srcRGBA[1], srcRGBA[2], dstRGBA[0], dstRGBA[1], dstRGBA[2], dstRGBA[3]);
        clTransformDestroy(C, transform);

        clProfileDestroy(C, srcProfile);
        clProfileDestroy(C, dstProfile);
        clContextDestroy(C);
    }

    printf("colorist-test Complete.\n");
    return 0;
}
