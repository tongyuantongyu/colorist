// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2018.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#include "colorist/profile.h"

#include "lcms2_plugin.h"

#include <math.h>
#include <string.h>

static void normalizeProfile(clProfile * profile);

clProfile * clProfileCreateStock(clProfileStock stock)
{
    clProfilePrimaries primaries;
    clProfileCurve curve;
    int maxLuminance;
    const char * description;
    COLORIST_ASSERT((stock >= 0) && (stock < CL_PS_COUNT));
    switch (stock) {
        case CL_PS_SRGB:
        default:
            primaries.red[0] = 0.64f;
            primaries.red[1] = 0.33f;
            primaries.green[0] = 0.30f;
            primaries.green[1] = 0.60f;
            primaries.blue[0] = 0.15f;
            primaries.blue[1] = 0.06f;
            primaries.white[0] = 0.3127f;
            primaries.white[1] = 0.3290f;
            curve.type = CL_PCT_GAMMA;
            curve.gamma = 2.4f;
            maxLuminance = 300;
            description = "SRGB";
            break;
    }
    return clProfileCreate(&primaries, &curve, maxLuminance, description);
}

clProfile * clProfileClone(clProfile * profile)
{
    uint8_t * bytes;
    cmsUInt32Number bytesNeeded;
    clProfile * clone;
    if (!cmsSaveProfileToMem(profile->handle, NULL, &bytesNeeded)) {
        return NULL;
    }
    bytes = (uint8_t *)malloc(bytesNeeded);
    if (!cmsSaveProfileToMem(profile->handle, bytes, &bytesNeeded)) {
        free(bytes);
        return NULL;
    }
    clone = clAllocate(clProfile);
    clone->handle = cmsOpenProfileFromMem(bytes, bytesNeeded);
    free(bytes);
    if (!clone->handle) {
        free(clone);
        return NULL;
    }
    clone->description = profile->description ? strdup(profile->description) : NULL;
    return clone;
}

clProfile * clProfileParse(const uint8_t * icc, int iccLen, const char * description)
{
    clProfile * profile = clAllocate(clProfile);
    profile->handle = cmsOpenProfileFromMem(icc, iccLen);
    if (!profile->handle) {
        free(profile);
        return NULL;
    }
    profile->description = description ? strdup(description) : NULL;
    return profile;
}

clProfile * clProfileCreate(clProfilePrimaries * primaries, clProfileCurve * curve, int maxLuminance, const char * description)
{
    clProfile * profile = clAllocate(clProfile);
    cmsToneCurve * curves[3];
    cmsCIExyYTRIPLE dstPrimaries;
    cmsCIExyY dstWhitePoint;

    dstPrimaries.Red.x = primaries->red[0];
    dstPrimaries.Red.y = primaries->red[1];
    dstPrimaries.Red.Y = 0.0f; // unused
    dstPrimaries.Green.x = primaries->green[0];
    dstPrimaries.Green.y = primaries->green[1];
    dstPrimaries.Green.Y = 0.0f; // unused
    dstPrimaries.Blue.x = primaries->blue[0];
    dstPrimaries.Blue.y = primaries->blue[1];
    dstPrimaries.Blue.Y = 0.0f; // unused
    dstWhitePoint.x = primaries->white[0];
    dstWhitePoint.y = primaries->white[1];
    dstWhitePoint.Y = 1.0f;

    curves[0] = cmsBuildGamma(NULL, curve->gamma);
    curves[1] = curves[0];
    curves[2] = curves[0];
    profile->handle = cmsCreateRGBProfile(&dstWhitePoint, &dstPrimaries, curves);
    cmsFreeToneCurve(curves[0]);
    if (!profile->handle) {
        free(profile);
        return NULL;
    }
    profile->description = description ? strdup(description) : NULL;
    return profile;
}

clProfile * clProfileCreateLinear(clProfile * origProfile)
{
    clProfile * profile;
    clProfilePrimaries primaries;
    clProfileCurve curve;
    char * description;
    int descriptionLen;
    int luminance;
    if (!clProfileQuery(origProfile, &primaries, NULL, &luminance)) {
        return NULL;
    }
    curve.type = CL_PCT_GAMMA;
    curve.gamma = 1.0f;
    descriptionLen = 20;
    if (origProfile->description)
        descriptionLen += strlen(origProfile->description);
    description = (char *)malloc(descriptionLen);
    description[0] = 0;
    if (origProfile->description)
        strcpy(description, origProfile->description);
    strcat(description, " (Linear)");
    profile = clProfileCreate(&primaries, &curve, luminance, description);
    free(description);
    return profile;
}

clBool clProfilePack(clProfile * profile, clRaw * out)
{
    cmsUInt32Number bytesNeeded;
    if (!cmsSaveProfileToMem(profile->handle, NULL, &bytesNeeded)) {
        return clFalse;
    }
    clRawRealloc(out, bytesNeeded);
    if (!cmsSaveProfileToMem(profile->handle, out->ptr, &bytesNeeded)) {
        clRawFree(out);
        return clFalse;
    }
    return clTrue;
}

void clProfileDestroy(clProfile * profile)
{
    free(profile->description);
    cmsCloseProfile(profile->handle);
    free(profile);
}

clBool clProfileQuery(clProfile * profile, clProfilePrimaries * primaries, clProfileCurve * curve, int * luminance)
{
    cmsMAT3 * chad;
    cmsMAT3 invChad;
    cmsMAT3 tmpColorants;
    cmsMAT3 colorants;
    cmsCIEXYZ src;
    cmsCIExyY dst;
    cmsCIEXYZ adaptedWhiteXYZ;
    cmsCIEXYZ * redXYZ   = (cmsCIEXYZ *)cmsReadTag(profile->handle, cmsSigRedColorantTag);
    cmsCIEXYZ * greenXYZ = (cmsCIEXYZ *)cmsReadTag(profile->handle, cmsSigGreenColorantTag);
    cmsCIEXYZ * blueXYZ  = (cmsCIEXYZ *)cmsReadTag(profile->handle, cmsSigBlueColorantTag);
    cmsCIEXYZ * whiteXYZ = (cmsCIEXYZ *)cmsReadTag(profile->handle, cmsSigMediaWhitePointTag);
    if ((redXYZ == NULL) || (greenXYZ == NULL) || (blueXYZ == NULL) || (whiteXYZ == NULL))
        return clFalse;

    _cmsVEC3init(&tmpColorants.v[0], redXYZ->X, greenXYZ->X, blueXYZ->X);
    _cmsVEC3init(&tmpColorants.v[1], redXYZ->Y, greenXYZ->Y, blueXYZ->Y);
    _cmsVEC3init(&tmpColorants.v[2], redXYZ->Z, greenXYZ->Z, blueXYZ->Z);

    chad = (cmsMAT3 *)cmsReadTag(profile->handle, cmsSigChromaticAdaptationTag);
    if ((chad != NULL) && _cmsMAT3inverse(chad, &invChad)) {
        cmsFloat64Number K;
        cmsCIExyY whiteXYY;
        cmsXYZ2xyY(&whiteXYY, whiteXYZ);
        if (cmsTempFromWhitePoint(&K, &whiteXYY) && (fabsf((float)K - 5000.0f) < 1.0f)) {
            // It's D50 in a profile with a chad tag, adapt it
            cmsVEC3 srcWP, dstWP;
            srcWP.n[VX] = whiteXYZ->X;
            srcWP.n[VY] = whiteXYZ->Y;
            srcWP.n[VZ] = whiteXYZ->Z;
            _cmsMAT3eval(&dstWP, &invChad, &srcWP);
            adaptedWhiteXYZ.X = dstWP.n[VX];
            adaptedWhiteXYZ.Y = dstWP.n[VY];
            adaptedWhiteXYZ.Z = dstWP.n[VZ];
        }
        _cmsMAT3per(&colorants, &invChad, &tmpColorants);
    } else {
        colorants = tmpColorants;
        adaptedWhiteXYZ = *whiteXYZ;
    }

    if (primaries) {
        src.X = colorants.v[0].n[VX];
        src.Y = colorants.v[1].n[VX];
        src.Z = colorants.v[2].n[VX];
        cmsXYZ2xyY(&dst, &src);
        primaries->red[0] = (float)dst.x;
        primaries->red[1] = (float)dst.y;
        src.X = colorants.v[0].n[VY];
        src.Y = colorants.v[1].n[VY];
        src.Z = colorants.v[2].n[VY];
        cmsXYZ2xyY(&dst, &src);
        primaries->green[0] = (float)dst.x;
        primaries->green[1] = (float)dst.y;
        src.X = colorants.v[0].n[VZ];
        src.Y = colorants.v[1].n[VZ];
        src.Z = colorants.v[2].n[VZ];
        cmsXYZ2xyY(&dst, &src);
        primaries->blue[0] = (float)dst.x;
        primaries->blue[1] = (float)dst.y;
        cmsXYZ2xyY(&dst, &adaptedWhiteXYZ);
        primaries->white[0] = (float)dst.x;
        primaries->white[1] = (float)dst.y;
    }

    if (curve) {
        cmsToneCurve * toneCurve = (cmsToneCurve *)cmsReadTag(profile->handle, cmsSigRedTRCTag);
        if (toneCurve) {
            int curveType = cmsGetToneCurveParametricType(toneCurve);
            float gamma = (float)cmsEstimateGamma(toneCurve, 1.0f);
            curve->type = (curveType == 1) ? CL_PCT_GAMMA : CL_PCT_COMPLEX;
            curve->gamma = gamma;
        } else {
            curve->type = CL_PCT_UNKNOWN;
            curve->gamma = 0.0f;
        }
    }

    if (luminance) {
        cmsCIEXYZ * lumi = (cmsCIEXYZ *)cmsReadTag(profile->handle, cmsSigLuminanceTag);
        if (lumi) {
            *luminance = (int)lumi->Y;
        } else {
            *luminance = 0;
        }
    }
    return clTrue;
}
