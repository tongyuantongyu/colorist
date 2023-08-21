//
// Created by TYTY on 2023-08-21 021.
//

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <filesystem>
#include <string_view>

extern "C" {
#include "colorist/colorist.h"
};

using namespace std::string_view_literals;

int handle_image(clContext * C, const std::filesystem::path & input)
{
    std::filesystem::path output = input;
    output.replace_extension("avif");

    C->inputFilename = input.c_str();
    C->outputFilename = output.c_str();

    return clContextConvert(C);
}

int wmain(int argc, wchar_t ** argv)
{
    if (argc != 2) {
        char buf[4096] {};
        size_t dont_care;
        wcstombs_s(&dont_care, buf, argv[0], 4095);
        fprintf(stderr, "Usage: %s <watch_dir>\n", buf);
        return 2;
    }

    clContext * C;
    clContextSystem system;

    system.alloc = clContextDefaultAlloc;
    system.free = clContextDefaultFree;
    system.log = clContextDefaultLog;
    system.error = clContextDefaultLogError;

    C = clContextCreate(&system);

    // -g pq
    C->params.curveType = CL_PCT_PQ;
    C->params.gamma = 1.0f;
    // -l 10000
    C->params.luminance = 10000;
    // -p bt2020
    {
        clProfilePrimaries tmpPrimaries;
        clContextGetStockPrimaries(C, "bt2020", &tmpPrimaries);
        C->params.primaries[0] = tmpPrimaries.red[0];
        C->params.primaries[1] = tmpPrimaries.red[1];
        C->params.primaries[2] = tmpPrimaries.green[0];
        C->params.primaries[3] = tmpPrimaries.green[1];
        C->params.primaries[4] = tmpPrimaries.blue[0];
        C->params.primaries[5] = tmpPrimaries.blue[1];
        C->params.primaries[6] = tmpPrimaries.white[0];
        C->params.primaries[7] = tmpPrimaries.white[1];
    }
    // -b 10
    C->params.bpc = 10;
    // -f avif
    C->params.formatName = "avif";
    // --yuv 420
    C->params.writeParams.yuvFormat = CL_YUVFORMAT_420;
    // --nclx 9,16,9
    C->params.writeParams.nclx[0] = 9;
    C->params.writeParams.nclx[1] = 16;
    C->params.writeParams.nclx[2] = 9;
    // --speed 6
    C->params.writeParams.speed = 6;
    // -q 70
    C->params.writeParams.quality = 70;

    std::filesystem::path dirname = argv[1];
    auto dir = CreateFileW(dirname.c_str(),
                           FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
                           nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Bad dir: %lu\n", GetLastError());
        return 1;
    }
    FILE_BASIC_INFO basicInfo;
    if (!GetFileInformationByHandleEx(dir, FileBasicInfo, &basicInfo, sizeof(basicInfo))) {
        fprintf(stderr, "Failed read info: %lu\n", GetLastError());
        return 1;
    }
    if (!(basicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "Not a dir: %lu\n", GetLastError());
        return 1;
    }

    auto info = std::make_unique<FILE_NOTIFY_INFORMATION[]>(1024 * 1024);
    auto size = 1024 * 1024 * sizeof(FILE_NOTIFY_INFORMATION);

    fprintf(stderr, "Listening for new files...\n");
    DWORD written_size = 0;
    while (ReadDirectoryChangesW(dir, info.get(), size, false, FILE_NOTIFY_CHANGE_LAST_WRITE, &written_size, nullptr, nullptr)) {
        fprintf(stderr, "Got some changes!\n");
        auto pinfo = info.get();
        while (written_size) {
            if (pinfo->Action == FILE_ACTION_MODIFIED) {
                fprintf(stderr, "Got modification changes!\n");
                std::filesystem::path file = dirname / std::wstring_view { pinfo->FileName, pinfo->FileNameLength };
                auto file_handle =
                    CreateFileW(file.c_str(), FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file_handle == INVALID_HANDLE_VALUE) {
                    fprintf(stderr, "Bad file: not ready for read.\n");
                    goto Next;
                }
                LARGE_INTEGER file_size {};
                if (!GetFileSizeEx(file_handle, &file_size) || file_size.QuadPart <= 0) {
                    CloseHandle(file_handle);
                    fprintf(stderr, "Bad file: not ready for read.\n");
                    goto Next;
                }

                CloseHandle(file_handle);
                auto ext = file.extension().string();
                if (ext.substr(0, 4) == ".jxr") {
                    handle_image(C, file);
                } else {
                    fprintf(stderr, "Not new jxr.\n");
                }
            }

        Next:
            if (pinfo->NextEntryOffset == 0) {
                break;
            }
            pinfo += pinfo->NextEntryOffset;
        }
    }

    auto err = GetLastError();
    fprintf(stderr, "unexpected: %lu\n", err);
    return err;
}
