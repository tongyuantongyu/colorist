// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2018.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#include "colorist/context.h"

#include "colorist/image.h"
#include "colorist/profile.h"

#include <stdio.h>
#include <string.h>

struct clImage * clContextRead(clContext * C, const char * filename, const char * iccOverride, const char ** outFormatName)
{
    clImage * image = NULL;
    clFormat * format;
    const char * formatName = clFormatDetect(C, filename);
    if (outFormatName)
        *outFormatName = formatName;
    if (!formatName) {
        return NULL;
    }
    if (!strcmp(formatName, "icc")) {
        // Someday, fix clFormatDetect() to not allow "icc" to return, and then this check can go away.
        return NULL;
    }

    clProfile * overrideProfile = NULL;
    if (iccOverride) {
        overrideProfile = clProfileRead(C, iccOverride);
        if (overrideProfile) {
            clContextLog(C, "profile", 1, "Overriding src profile with file: %s", iccOverride);
        } else {
            clContextLogError(C, "Bad ICC override file [-i]: %s", iccOverride);
            return NULL;
        }
    }

    clRaw input = CL_RAW_EMPTY;
    if (!clRawReadFile(C, &input, filename)) {
        return clFalse;
    }

    // Clear this out, only some of the format readers actually populate anything in here
    memset(&C->readExtraInfo, 0, sizeof(C->readExtraInfo));

    format = clContextFindFormat(C, formatName);
    COLORIST_ASSERT(format);
    if (format->readFunc) {
        image = format->readFunc(C, formatName, overrideProfile, &input);
    } else {
        clContextLogError(C, "Unimplemented file reader '%s'", formatName);
    }

    if (overrideProfile) {
        // Just in case the read plugin is a bad citizen
        if (image) {
            if (image->profile == overrideProfile) {
                // if the pointers match exactly, let it take ownership
                overrideProfile = NULL;
            } else if (!clProfileMatches(C, image->profile, overrideProfile)) {
                clProfileDestroy(C, image->profile);
                image->profile = overrideProfile; // take ownership
                overrideProfile = NULL;
            }
        }

        // There is a chance we lost ownership, so check if it is still here
        if (overrideProfile) {
            clProfileDestroy(C, overrideProfile);
            overrideProfile = NULL;
        }
    }

    if (C->enforceLuminance) {
        if (!image->profile) {
            clContextLogError(C, "No profile for input, cannot enforce luminance");
        } else {
            clProfileSetLuminance(C, image->profile, C->defaultLuminance);
            clContextLog(C, "profile", 1, "Overriding profile luminance as: %d nits", C->defaultLuminance);
        }
    }

    clRawFree(C, &input);
    return image;
}

clBool clContextWrite(clContext * C, struct clImage * image, const char * filename, const char * formatName, clWriteParams * writeParams)
{
    clBool result = clFalse;

    if (formatName == NULL) {
        formatName = clFormatDetect(C, filename);
        if (formatName == NULL) {
            clContextLogError(C, "Unknown output file format '%s', please specify with -f", filename);
            return clFalse;
        }
    }

    clFormat * format = clContextFindFormat(C, formatName);
    COLORIST_ASSERT(format);

    if (format->writeFunc) {
        clRaw output = CL_RAW_EMPTY;
        if (format->writeFunc(C, image, formatName, &output, writeParams)) {
            if (clRawWriteFile(C, &output, filename)) {
                result = clTrue;
            }
        }
        clRawFree(C, &output);
    } else {
        clContextLogError(C, "Unimplemented file writer '%s'", formatName);
    }
    return result;
}

char * clContextWriteURI(struct clContext * C, clImage * image, const char * formatName, clWriteParams * writeParams)
{
    char * output = NULL;

    clFormat * format = clContextFindFormat(C, formatName);
    if (!format) {
        clContextLogError(C, "Unknown format: %s", formatName);
        return NULL;
    }

    if (format->writeFunc) {
        clRaw dst = CL_RAW_EMPTY;
        if (format->writeFunc(C, image, formatName, &dst, writeParams)) {
            char prefix[512];
            size_t prefixLen = sprintf(prefix, "data:%s;base64,", format->mimeType);

            char * b64 = clRawToBase64(C, &dst);
            if (!b64) {
                clRawFree(C, &dst);
                return NULL;
            }
            size_t b64Len = strlen(b64);

            output = clAllocate(prefixLen + b64Len + 1);
            memcpy(output, prefix, prefixLen);
            memcpy(output + prefixLen, b64, b64Len);
            output[prefixLen + b64Len] = 0;

            clFree(b64);
        }
        clRawFree(C, &dst);
    } else {
        clContextLogError(C, "Unimplemented file writer '%s'", formatName);
    }

    return output;
}
