// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#include "colorist/image.h"

#include "colorist/context.h"
#include "colorist/profile.h"
#include "colorist/transform.h"

#include "avif/avif.h"

#include <string.h>

static clProfile * nclxToclProfile(struct clContext * C, avifImage * avif);
static clBool clProfileToNclx(struct clContext * C, struct clProfile * profile, avifImage * avif);
static void logAvifImage(struct clContext * C, avifImage * avif, avifIOStats * ioStats);

clBool clFormatDetectAVIF(struct clContext * C, struct clFormat * format, struct clRaw * input);
struct clImage * clFormatReadAVIF(struct clContext * C, const char * formatName, struct clProfile * overrideProfile, struct clRaw * input);
clBool clFormatWriteAVIF(struct clContext * C,
                         struct clImage * image,
                         const char * formatName,
                         struct clRaw * output,
                         struct clWriteParams * writeParams);

clBool clFormatDetectAVIF(struct clContext * C, struct clFormat * format, struct clRaw * input)
{
    COLORIST_UNUSED(C);
    COLORIST_UNUSED(format);

    avifROData header;
    header.data = input->ptr;
    header.size = input->size;
    if (avifPeekCompatibleFileType(&header)) {
        return clTrue;
    }
    return clFalse;
}

struct clImage * clFormatReadAVIF(struct clContext * C, const char * formatName, struct clProfile * overrideProfile, struct clRaw * input)
{
    COLORIST_UNUSED(formatName);
    COLORIST_UNUSED(input);
    COLORIST_UNUSED(C);

    clImage * image = NULL;
    clProfile * profile = NULL;

    avifROData raw;
    raw.data = input->ptr;
    raw.size = input->size;

    Timer t;
    timerStart(&t);

    avifDecoder * decoder = avifDecoderCreate();
    if (C->params.readCodec) {
        decoder->codecChoice = avifCodecChoiceFromName(C->params.readCodec);
    }
    const char * codecName = avifCodecName(decoder->codecChoice, AVIF_CODEC_FLAG_CAN_DECODE);
    if (codecName == NULL) {
        clContextLogError(C, "No AV1 codec available for decoding");
        goto readCleanup;
    }
    clContextLog(C, "avif", 1, "AV1 codec (decode): %s", codecName);

    avifDecoderSetIOMemory(decoder, raw.data, raw.size);
    avifResult decodeResult = avifDecoderParse(decoder);
    if (decodeResult != AVIF_RESULT_OK) {
        clContextLogError(C, "Failed to parse AVIF (%s) %s", avifResultToString(decodeResult), decoder->diag.error);
        if (decoder->diag.error) {
            strncpy(C->readExtraInfo.diagnosticError, decoder->diag.error, CL_DIAGNOSTIC_ERROR_SIZE);
            C->readExtraInfo.diagnosticError[CL_DIAGNOSTIC_ERROR_SIZE - 1] = 0;
        }
        goto readCleanup;
    }

    uint32_t frameIndex = 0;
    if (decoder->imageCount > 1) {
        frameIndex = C->params.frameIndex;
        clContextLog(C, "avif", 1, "AVIF contains %d frames, decoding frame %d.", decoder->imageCount, frameIndex);
        uint32_t nearestKeyframe = avifDecoderNearestKeyframe(decoder, frameIndex);
        if (nearestKeyframe != frameIndex) {
            clContextLog(C, "avif", 1, "Nearest keyframe is frame %d, so %d total frames must be decoded.", nearestKeyframe, 1 + frameIndex - nearestKeyframe);
        }
    }
    avifResult frameResult = avifDecoderNthImage(decoder, frameIndex);
    if (frameResult != AVIF_RESULT_OK) {
        clContextLogError(C, "Failed to get AVIF frame %d (%s) %s", frameIndex, avifResultToString(frameResult), decoder->diag.error);
        if (decoder->diag.error) {
            strncpy(C->readExtraInfo.diagnosticError, decoder->diag.error, CL_DIAGNOSTIC_ERROR_SIZE);
            C->readExtraInfo.diagnosticError[CL_DIAGNOSTIC_ERROR_SIZE - 1] = 0;
        }
        goto readCleanup;
    }

    avifImage * avif = decoder->image;

    C->readExtraInfo.decodeCodecSeconds = timerElapsedSeconds(&t);

    if (overrideProfile) {
        profile = clProfileClone(C, overrideProfile);
    } else if (avif->icc.data && avif->icc.size) {
        profile = clProfileParse(C, avif->icc.data, avif->icc.size, NULL);
        if (!profile) {
            clContextLogError(C, "Failed parse ICC profile chunk");
            goto readCleanup;
        }
    } else {
        profile = nclxToclProfile(C, avif);
    }

    logAvifImage(C, avif, &decoder->ioStats);

    clImageLogCreate(C, avif->width, avif->height, avif->depth, profile);
    image = clImageCreate(C, avif->width, avif->height, avif->depth, profile);

    timerStart(&t);
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    if (avifImageUsesU16(avif)) {
        clImagePrepareWritePixels(C, image, CL_PIXELFORMAT_U16);

        rgb.pixels = (uint8_t *)image->pixelsU16;
        rgb.rowBytes = image->width * sizeof(uint16_t) * CL_CHANNELS_PER_PIXEL;
        avifImageYUVToRGB(avif, &rgb);
    } else {
        clImagePrepareWritePixels(C, image, CL_PIXELFORMAT_U8);

        rgb.pixels = image->pixelsU8;
        rgb.rowBytes = image->width * sizeof(uint8_t) * CL_CHANNELS_PER_PIXEL;
        avifImageYUVToRGB(avif, &rgb);
    }
    C->readExtraInfo.decodeYUVtoRGBSeconds = timerElapsedSeconds(&t);

    if (decoder->imageCount > 1) {
        C->readExtraInfo.frameIndex = (int)frameIndex;
        C->readExtraInfo.frameCount = decoder->imageCount;
    }

    if (decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) {
        avifCleanApertureBox * clap = &decoder->image->clap;
        int * crop = C->readExtraInfo.crop;

        // see ISO/IEC 14496-12:2015 12.1.4.1

        int croppedW = (int)clap->widthN / (int)clap->widthD;
        int croppedH = (int)clap->heightN / (int)clap->heightD;
        int offX = (int)clap->horizOffN / (int)clap->horizOffD;
        int offY = (int)clap->vertOffN / (int)clap->vertOffD;
        int halfCroppedW = (croppedW - 1) / 2;
        int halfCroppedH = (croppedH - 1) / 2;
        int centerX = offX + (decoder->image->width - 1) / 2;
        int centerY = offY + (decoder->image->height - 1) / 2;
        int topLeftX = centerX - halfCroppedW;
        int topLeftY = centerY - halfCroppedH;

        crop[0] = topLeftX;
        crop[1] = topLeftY;
        crop[2] = croppedW;
        crop[3] = croppedH;
    }
    if (decoder->image->transformFlags & AVIF_TRANSFORM_IROT) {
        switch (decoder->image->irot.angle) { // in ccw rotations
            case 1:
                C->readExtraInfo.cwRotationsNeeded = 3;
                break;
            case 2:
                C->readExtraInfo.cwRotationsNeeded = 2;
                break;
            case 3:
                C->readExtraInfo.cwRotationsNeeded = 1;
                break;
        }
    }
    if (decoder->image->transformFlags & AVIF_TRANSFORM_IMIR) {
        C->readExtraInfo.mirrorNeeded = 1 + decoder->image->imir.axis;
    }
readCleanup:
    avifDecoderDestroy(decoder);
    if (profile) {
        clProfileDestroy(C, profile);
    }

    return image;
}

clBool clFormatWriteAVIF(struct clContext * C, struct clImage * image, const char * formatName, struct clRaw * output, struct clWriteParams * writeParams)
{
    COLORIST_UNUSED(formatName);

    clBool writeResult = clTrue;
    avifImage * avif = NULL;
    avifEncoder * encoder = NULL;

    clRaw rawProfile = CL_RAW_EMPTY;
    if (!clProfilePack(C, image->profile, &rawProfile)) {
        clContextLogError(C, "Failed to create ICC profile");
        writeResult = clFalse;
        goto writeCleanup;
    }

    avifPixelFormat avifYUVFormat;
    switch (writeParams->yuvFormat) {
        case CL_YUVFORMAT_444:
            avifYUVFormat = AVIF_PIXEL_FORMAT_YUV444;
            break;
        case CL_YUVFORMAT_422:
            avifYUVFormat = AVIF_PIXEL_FORMAT_YUV422;
            break;
        case CL_YUVFORMAT_420:
            avifYUVFormat = AVIF_PIXEL_FORMAT_YUV420;
            break;
        case CL_YUVFORMAT_400:
            avifYUVFormat = AVIF_PIXEL_FORMAT_YUV400;
            break;
        case CL_YUVFORMAT_INVALID:
        default:
            clContextLogError(C, "Unable to choose AVIF YUV format");
            writeResult = clFalse;
            goto writeCleanup;
    }

    avif = avifImageCreate(image->width, image->height, image->depth, avifYUVFormat);

    if (writeParams->writeProfile) {
        if (writeParams->nclx[0] && writeParams->nclx[1] && writeParams->nclx[2]) {
            avif->colorPrimaries = (uint16_t)writeParams->nclx[0];
            avif->transferCharacteristics = (uint16_t)writeParams->nclx[1];
            avif->matrixCoefficients = (uint16_t)writeParams->nclx[2];
            avif->yuvRange = image->depth > 8 ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;
            clContextLog(C,
                         "avif",
                         1,
                         "Forcing colr box (nclx): C: %d / T: %d / M: %d / F: 0x%x",
                         avif->colorPrimaries,
                         avif->transferCharacteristics,
                         avif->matrixCoefficients,
                         avif->yuvRange);
        } else if (clProfileToNclx(C, image->profile, avif)) {
            clContextLog(C,
                         "avif",
                         1,
                         "Writing colr box (nclx): C: %d / T: %d / M: %d / F: 0x%x",
                         avif->colorPrimaries,
                         avif->transferCharacteristics,
                         avif->matrixCoefficients,
                         avif->yuvRange);
        } else {
            clContextLog(C, "avif", 1, "Writing colr box (icc): %u bytes", (uint32_t)rawProfile.size);
            avifImageSetProfileICC(avif, rawProfile.ptr, rawProfile.size);
        }
        avif->clli.maxCLL = (uint16_t)roundf(clImagePeakLuminance(C, image));
        clContextLog(C, "avif", 1, "Writing clli box : maxCLL: %u", (uint32_t)avif->clli.maxCLL);
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY;
    if (avifImageUsesU16(avif)) {
        clImagePrepareReadPixels(C, image, CL_PIXELFORMAT_U16);

        rgb.pixels = (uint8_t *)image->pixelsU16;
        rgb.rowBytes = image->width * sizeof(uint16_t) * CL_CHANNELS_PER_PIXEL;
        avifImageRGBToYUV(avif, &rgb);
    } else {
        clImagePrepareReadPixels(C, image, CL_PIXELFORMAT_U8);

        rgb.pixels = image->pixelsU8;
        rgb.rowBytes = image->width * sizeof(uint8_t) * CL_CHANNELS_PER_PIXEL;
        avifImageRGBToYUV(avif, &rgb);
    }

    avifRWData avifOutput = AVIF_DATA_EMPTY;

    encoder = avifEncoderCreate();
    if (writeParams->codec) {
        encoder->codecChoice = avifCodecChoiceFromName(writeParams->codec);
    }
    const char * codecName = avifCodecName(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
    if (codecName == NULL) {
        clContextLogError(C, "No AV1 codec available for encoding");
        goto writeCleanup;
    }
    clContextLog(C, "avif", 1, "AV1 codec (encode): %s", codecName);

    encoder->maxThreads = C->jobs;
    if ((writeParams->quantizerMin == -1) && (writeParams->quantizerMax == -1)) {
        int quality = writeParams->quality ? writeParams->quality : 100; // consider 0 to be lossless (100)

        encoder->quality = quality;
        encoder->minQuantizer = 0;
        encoder->maxQuantizer = 63;
        clContextLog(C,
                     "avif",
                     1,
                     "Encoding quality: %d",
                     quality,
                     (quality == 100) ? " [Lossless]" : "");
    } else {
        encoder->minQuantizer = writeParams->quantizerMin;
        encoder->maxQuantizer = writeParams->quantizerMax;
        clContextLog(C,
                     "avif",
                     1,
                     "Encoding quantizer (0=lossless, 63=worst) min/max: %d/%d    (explicit)",
                     encoder->minQuantizer,
                     encoder->maxQuantizer);
    }
    encoder->tileRowsLog2 = writeParams->tileRowsLog2;
    encoder->tileColsLog2 = writeParams->tileColsLog2;
    if (encoder->tileRowsLog2 || encoder->tileColsLog2) {
        clContextLog(C, "avif", 1, "Encoding tiling (log2): 2^%d rows / 2^%d cols", encoder->tileRowsLog2, encoder->tileColsLog2);
    } else {
        clContextLog(C, "avif", 1, "Encoding tiling (log2): disabled");
    }
    encoder->speed = writeParams->speed;
    if (encoder->speed == -1) {
        clContextLog(C, "avif", 1, "Encoding speed (0=BestQuality, 10=Fastest): default (%s)", codecName);
    } else {
        clContextLog(C, "avif", 1, "Encoding speed (0=BestQuality, 10=Fastest): %d", encoder->speed);
    }
    avifResult encodeResult = avifEncoderWrite(encoder, avif, &avifOutput);
    if (encodeResult != AVIF_RESULT_OK) {
        clContextLogError(C, "AVIF encoder failed (%s)", avifResultToString(encodeResult));
        writeResult = clFalse;
        goto writeCleanup;
    }

    if (!avifOutput.data || !avifOutput.size) {
        clContextLogError(C, "AVIF encoder returned empty data");
        writeResult = clFalse;
        goto writeCleanup;
    }

    clRawSet(C, output, avifOutput.data, avifOutput.size);

    logAvifImage(C, avif, &encoder->ioStats);

writeCleanup:
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    if (avif) {
        avifImageDestroy(avif);
    }
    avifRWDataFree(&avifOutput);
    clRawFree(C, &rawProfile);
    return writeResult;
}

static clProfile * nclxToclProfile(struct clContext * C, avifImage * avif)
{
    clProfilePrimaries primaries;
    clProfileCurve curve;
    int maxLuminance;

    // Defaults
    clContextGetStockPrimaries(C, "bt709", &primaries);
    curve.type = CL_PCT_GAMMA;
    curve.gamma = 2.2f;
    curve.implicitScale = 1.0f;
    maxLuminance = avif->clli.maxCLL == 0 ? CL_LUMINANCE_UNSPECIFIED : avif->clli.maxCLL;

    float prim[8];
    avifColorPrimariesGetValues(avif->colorPrimaries, prim);
    primaries.red[0] = prim[0];
    primaries.red[1] = prim[1];
    primaries.green[0] = prim[2];
    primaries.green[1] = prim[3];
    primaries.blue[0] = prim[4];
    primaries.blue[1] = prim[5];
    primaries.white[0] = prim[6];
    primaries.white[1] = prim[7];

    switch (avif->transferCharacteristics) {
        case AVIF_TRANSFER_CHARACTERISTICS_HLG:
            curve.type = CL_PCT_HLG;
            curve.gamma = 1.0f;
            break;
        case AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084:
            curve.type = CL_PCT_PQ;
            curve.gamma = 1.0f;
            if (maxLuminance == CL_LUMINANCE_UNSPECIFIED) {
                maxLuminance = 10000;
            }
            break;
        case AVIF_TRANSFER_CHARACTERISTICS_BT470M:
            curve.type = CL_PCT_GAMMA;
            curve.gamma = 2.2f;
            break;
        case AVIF_TRANSFER_CHARACTERISTICS_BT470BG:
            curve.type = CL_PCT_GAMMA;
            curve.gamma = 2.8f;
            break;
        case AVIF_TRANSFER_CHARACTERISTICS_SRGB:
            curve.type = CL_PCT_SRGB;
            curve.gamma = 1.0f;
            break;

        case AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN:
        case AVIF_TRANSFER_CHARACTERISTICS_BT709:
        case AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED:
        case AVIF_TRANSFER_CHARACTERISTICS_BT601:
        case AVIF_TRANSFER_CHARACTERISTICS_SMPTE240:
        case AVIF_TRANSFER_CHARACTERISTICS_LINEAR:
        case AVIF_TRANSFER_CHARACTERISTICS_LOG100:
        case AVIF_TRANSFER_CHARACTERISTICS_LOG100_SQRT10:
        case AVIF_TRANSFER_CHARACTERISTICS_IEC61966:
        case AVIF_TRANSFER_CHARACTERISTICS_BT1361:
        case AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT:
        case AVIF_TRANSFER_CHARACTERISTICS_BT2020_12BIT:
        case AVIF_TRANSFER_CHARACTERISTICS_SMPTE428:
            clContextLog(C,
                         "avif",
                         1,
                         "WARNING: Unsupported colr (nclx) transfer_characteristics %d, using gamma:%1.1f, lum:%d",
                         avif->transferCharacteristics,
                         curve.gamma,
                         maxLuminance);
            break;
    }

    char gammaString[64];
    if ((curve.type == CL_PCT_GAMMA) || (curve.type == CL_PCT_COMPLEX)) {
        sprintf(gammaString, "(%.2g)", curve.gamma);
    } else {
        gammaString[0] = 0;
    }

    char maxLumString[64];
    if (maxLuminance == CL_LUMINANCE_UNSPECIFIED) {
        strcpy(maxLumString, "Unspecified");
    } else {
        sprintf(maxLumString, "%d", maxLuminance);
    }

    clContextLog(C,
                 "avif",
                 1,
                 "nclx to ICC: Primaries: (r:%.4g,%.4g g:%.4g,%.4g b:%.4g,%.4g w:%.4g,%.4g), Curve: %s%s, maxLum: %s",
                 primaries.red[0],
                 primaries.red[1],
                 primaries.green[0],
                 primaries.green[1],
                 primaries.blue[0],
                 primaries.blue[1],
                 primaries.white[0],
                 primaries.white[1],
                 clProfileCurveTypeToString(C, curve.type),
                 gammaString,
                 maxLumString);

    char * description = clGenerateDescription(C, &primaries, &curve, maxLuminance);
    clProfile * profile = clProfileCreate(C, &primaries, &curve, maxLuminance, description);
    clFree(description);
    return profile;
}

static clBool clProfileToNclx(struct clContext * C, struct clProfile * profile, avifImage * avif)
{
    clProfilePrimaries primaries;
    clProfileCurve curve;
    int luminance = 0;
    if (!clProfileQuery(C, profile, &primaries, &curve, &luminance)) {
        return clFalse;
    }

    const char * primariesName = NULL;
    float floatPrimaries[8];
    floatPrimaries[0] = primaries.red[0];
    floatPrimaries[1] = primaries.red[1];
    floatPrimaries[2] = primaries.green[0];
    floatPrimaries[3] = primaries.green[1];
    floatPrimaries[4] = primaries.blue[0];
    floatPrimaries[5] = primaries.blue[1];
    floatPrimaries[6] = primaries.white[0];
    floatPrimaries[7] = primaries.white[1];
    avifColorPrimaries foundColorPrimaries = avifColorPrimariesFind(floatPrimaries, &primariesName);
    if (foundColorPrimaries == AVIF_COLOR_PRIMARIES_UNKNOWN) {
        return clFalse;
    }

    avifMatrixCoefficients matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
    switch (foundColorPrimaries) {
        case AVIF_COLOR_PRIMARIES_BT709:
        case AVIF_COLOR_PRIMARIES_BT470BG:
        case AVIF_COLOR_PRIMARIES_UNSPECIFIED:
        case AVIF_COLOR_PRIMARIES_BT601:
            matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
            break;
        case AVIF_COLOR_PRIMARIES_BT2020:
            matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;
            break;

        case AVIF_COLOR_PRIMARIES_UNKNOWN:
        case AVIF_COLOR_PRIMARIES_BT470M:
        case AVIF_COLOR_PRIMARIES_SMPTE240:
        case AVIF_COLOR_PRIMARIES_GENERIC_FILM:
        case AVIF_COLOR_PRIMARIES_XYZ:
        case AVIF_COLOR_PRIMARIES_SMPTE431:
        case AVIF_COLOR_PRIMARIES_SMPTE432:
        case AVIF_COLOR_PRIMARIES_EBU3213:
            matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL;
            break;
    }

    const char * transferCharacteristicsName = NULL;
    avifTransferCharacteristics transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN;
    if ((curve.type == CL_PCT_PQ) && (luminance == 10000)) {
        transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
        transferCharacteristicsName = "PQ";
    } else {
        if (luminance != CL_LUMINANCE_UNSPECIFIED) {
            // Other than PQ, there is no current way to specify a max luminance via nclx. Bail out!
            return clFalse;
        }

        if (curve.type == CL_PCT_HLG) {
            transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_HLG;
            transferCharacteristicsName = "HLG";
        } else if (curve.type == CL_PCT_SRGB) {
            transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
            transferCharacteristicsName = "SRGB";
        } else if (curve.type == CL_PCT_GAMMA) {
            if (fabsf(curve.gamma - 2.2f) < 0.001f) {
                transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_BT470M;
                transferCharacteristicsName = "2.2g";
            } else if (fabsf(curve.gamma - 2.8f) < 0.001f) {
                transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_BT470BG;
                transferCharacteristicsName = "2.8g";
            }
        }
    }

    if (transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN) {
        return clFalse;
    }

    clContextLog(C, "avif", 1, "%s %s color profile detected; switching to nclx colr box.", primariesName, transferCharacteristicsName);
    avif->colorPrimaries = (uint16_t)foundColorPrimaries;
    avif->transferCharacteristics = (uint16_t)transferCharacteristics;
    avif->matrixCoefficients = (uint16_t)matrixCoefficients;
    avif->yuvRange = AVIF_RANGE_FULL;
    return clTrue;
}

static void logAvifImage(struct clContext * C, avifImage * avif, avifIOStats * ioStats)
{
    const char * yuvFormatString = "Unknown";
    clYUVFormat yuvFormat = CL_YUVFORMAT_INVALID;
    switch (avif->yuvFormat) {
        case AVIF_PIXEL_FORMAT_YUV444:
            yuvFormat = CL_YUVFORMAT_444;
            break;
        case AVIF_PIXEL_FORMAT_YUV422:
            yuvFormat = CL_YUVFORMAT_422;
            break;
        case AVIF_PIXEL_FORMAT_YUV420:
            yuvFormat = CL_YUVFORMAT_420;
            break;
        case AVIF_PIXEL_FORMAT_YUV400:
            yuvFormat = CL_YUVFORMAT_400;
            break;
        case AVIF_PIXEL_FORMAT_NONE:
        default:
            break;
    }
    if (yuvFormat != CL_YUVFORMAT_INVALID) {
        yuvFormatString = clYUVFormatToString(C, yuvFormat);
    }

    clContextLog(C, "avif", 1, "YUV: %s / ColorOBU: %zub / AlphaOBU: %zub", yuvFormatString, ioStats->colorOBUSize, ioStats->alphaOBUSize);
}
