//
// Created by TYTY on 2023-08-21 021.
//

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj_core.h>
#include <shlwapi.h>

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

extern "C" {
#include "colorist/colorist.h"
};

using namespace std::string_view_literals;

void put_sv(std::vector<char> & vec, std::string_view sv)
{
    std::copy(sv.begin(), sv.end(), std::back_inserter(vec));
}

std::vector<char> make_html(const std::filesystem::path & input)
{
    wchar_t url_buf[4096];
    DWORD length = 4095;
    if (UrlCreateFromPathW(input.c_str(), url_buf, &length, 0) != S_OK) {
        fprintf(stderr, "Failed create file uri.\n");
        return {};
    }

    // Create temporary buffer for HTML header...
    std::vector<char> buf_mem;
    buf_mem.reserve(1024 + length * 6);

    // Get clipboard id for HTML format...
    static int cfid = 0;
    if (!cfid)
        cfid = RegisterClipboardFormat("HTML Format");

    // Create a template string for the HTML header...
    put_sv(buf_mem,
           "Version:0.9\r\n"
           "StartHTML:0000000000\r\n"
           "EndHTML:0000000000\r\n"
           "StartFragment:0000000000\r\n"
           "EndFragment:0000000000\r\n"
           "<html>\r\n"
           "<body>\r\n"
           "<!--StartFragment-->");

    // Append the HTML...

    put_sv(buf_mem, "<img src=\"");
    {
        char url_u8_buf[8192];
        size_t written = WideCharToMultiByte(CP_UTF8, 0, url_buf, length, url_u8_buf, 8192, nullptr, nullptr);
        if (written == 0) {
            fprintf(stderr, "Failed convert file uri.\n");
            return {};
        }
        for (size_t i = 0; i < written; ++i) {
            if (url_u8_buf[i] >= 0) {
                buf_mem.push_back(url_u8_buf[i]);
            } else {
                auto byte = uint8_t(url_u8_buf[i]);
                char tmp[2];
                std::to_chars(tmp, tmp + 2, byte, 16);
                buf_mem.push_back('%');
                put_sv(buf_mem, { tmp, tmp + 2 });
            }
        }
    }

    put_sv(buf_mem, R"("/>)");
    // Finish up the HTML format...
    put_sv(buf_mem,
           "<!--EndFragment-->\r\n"
           "</body>\r\n"
           "</html>");
    buf_mem.push_back('\0');

    // Now go back, calculate all the lengths, and write out the
    // necessary header information. Note, wsprintf() truncates the
    // string when you overwrite it so you follow up with code to replace
    // the 0 appended at the end with a '\r'...
    auto buf = buf_mem.data();
    auto * ptr = strstr(buf, "StartHTML");
    wsprintfA(ptr + 10, "%010u", strstr(buf, "<html>") - buf);
    *(ptr + 10 + 10) = '\r';

    ptr = strstr(buf, "EndHTML");
    wsprintfA(ptr + 8, "%010u", buf_mem.size() - 1);
    *(ptr + 8 + 10) = '\r';

    ptr = strstr(buf, "StartFragment");
    wsprintfA(ptr + 14, "%010u", strstr(buf, "<!--StartFrag") - buf + 20);
    *(ptr + 14 + 10) = '\r';

    ptr = strstr(buf, "EndFragment");
    wsprintfA(ptr + 12, "%010u", strstr(buf, "<!--EndFrag") - buf);
    *(ptr + 12 + 10) = '\r';

    return buf_mem;
}

std::vector<char> make_qq(const std::filesystem::path & input)
{
    std::vector<char> buf_mem;
    buf_mem.reserve(1024 + input.wstring().size() * 6);
    put_sv(buf_mem, R"(<QQRichEditFormat><Info version="1001"></Info><EditElement type="1" filepath=")");
    {
        uint32_t tmp = 0;
        for (wchar_t chr : input.wstring()) {
            if (chr < 0x80) {
                buf_mem.push_back(char(chr));
                continue;
            } else if (0xD800 <= chr && chr < 0xDC00) {
                tmp = (uint32_t(chr) - 0xD800) * 400;
                continue;
            } else if (0xDC00 <= chr && chr < 0xE000) {
                tmp += (uint32_t(chr) - 0xDC00);
            } else {
                tmp = uint32_t(chr);
            }

            put_sv(buf_mem, "&#x");
            char rep[6];
            auto res = std::to_chars(rep, rep + 6, tmp, 16);
            put_sv(buf_mem, { rep, size_t(res.ptr - rep) });
            buf_mem.push_back(';');
        }
    }

    put_sv(buf_mem, R"(" shortcut=""></EditElement></QQRichEditFormat>)");
    buf_mem.push_back('\0');
    return buf_mem;
}

int send_clipboard(const std::filesystem::path & file)
{
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        {
            auto html = make_html(file);
            if (html.empty()) {
                fprintf(stderr, "Failed building HTML Format.\n");
                return 1;
            }
            HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, html.size());
            memcpy((char *)GlobalLock(hText), html.data(), html.size());
            GlobalUnlock(hText);

            static int cfid_html = 0;
            if (!cfid_html)
                cfid_html = RegisterClipboardFormat("HTML Format");
            SetClipboardData(cfid_html, hText);
        }

        {
            auto qq = make_qq(file);
            if (qq.empty()) {
                fprintf(stderr, "Failed building QQ Format.\n");
                return 1;
            }
            HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, qq.size());
            memcpy((char *)GlobalLock(hText), qq.data(), qq.size());
            GlobalUnlock(hText);

            static int cfid_qq = 0;
            if (!cfid_qq)
                cfid_qq = RegisterClipboardFormat("QQ_Unicode_RichEdit_Format");
            SetClipboardData(cfid_qq, hText);
        }

        {
            HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4);
            memcpy((char *)GlobalLock(hText), "\x01\x00\x00\x00", 4);
            GlobalUnlock(hText);

            static int cfid_drop = 0;
            if (!cfid_drop)
                cfid_drop = RegisterClipboardFormat("Preferred DropEffect");
            SetClipboardData(cfid_drop, hText);
        }

        {
            struct drop_file
            {
                DROPFILES hdr;
                wchar_t c[1];
            };
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT,
                                       sizeof(DROPFILES) + sizeof(wchar_t) * (file.wstring().size() + 2));
            auto data = (drop_file *)GlobalLock(hMem);
            data->hdr = { sizeof(DROPFILES), { 0, 0 }, false, true };
            memcpy(&data->c, file.c_str(), file.wstring().size() * sizeof(wchar_t));

            GlobalUnlock(hMem);
            SetClipboardData(CF_HDROP, hMem);
        }

        CloseClipboard();
    }

    fprintf(stderr, "Image copied.\n");
    return 0;
}

std::filesystem::path rewrite_path(const std::filesystem::path & p)
{
    std::vector<wchar_t> buf_mem;
    buf_mem.reserve(p.wstring().size() * 4);
    for (wchar_t chr : p.wstring()) {
        if (chr < 0x80) {
            buf_mem.push_back(chr);
            continue;
        }
        char rep[4];
        auto res = std::to_chars(rep, rep + 4, uint16_t(chr), 16);
        std::copy(rep, res.ptr, std::back_inserter(buf_mem));
    }

    return { std::wstring_view { buf_mem.data(), buf_mem.size() } };
}

int handle_image(clContext * C, const std::filesystem::path & input)
{
    std::filesystem::path output = input;
    output.replace_extension("avif");
    output = rewrite_path(output);

    C->inputFilename = input.c_str();
    C->outputFilename = output.c_str();

    if (clContextConvert(C) != 0) {
        return -1;
    }

    return send_clipboard(output);
}

int wmain(int argc, wchar_t ** argv)
{
    std::filesystem::path dirname;
    if (argc == 1) {
        wchar_t buf[4096] {};
        auto len = ExpandEnvironmentStringsW(L"%userprofile%\\Videos\\Captures", buf, 4095);
        if (len == 0) {
            fprintf(stderr, "Can not get default dir: %lu\n", GetLastError());
            return 2;
        }

        dirname = std::wstring_view {buf, len - 1};
        fprintf(stderr, "Watching default dir: %%userprofile%%\\Videos\\Captures\n");
    } else {
        dirname = argv[1];
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
    // --yuv 444
    C->params.writeParams.yuvFormat = CL_YUVFORMAT_444;
    // --nclx 9,16,9
    C->params.writeParams.nclx[0] = 9;
    C->params.writeParams.nclx[1] = 16;
    C->params.writeParams.nclx[2] = 9;
    // --speed 6
    C->params.writeParams.speed = 6;
    // -q 70
    C->params.writeParams.quality = 80;

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
    std::filesystem::path last;
    while (ReadDirectoryChangesW(dir, info.get(), size, false, FILE_NOTIFY_CHANGE_LAST_WRITE, &written_size, nullptr, nullptr)) {
        fprintf(stderr, "Got some changes!\n");
        auto pinfo = info.get();
        while (written_size) {
            if (pinfo->Action == FILE_ACTION_MODIFIED) {
                fprintf(stderr, "Got modification changes!\n");
                std::filesystem::path file = dirname / std::wstring_view { pinfo->FileName, pinfo->FileNameLength / sizeof(wchar_t) };
                if (file == last) {
                    fprintf(stderr, "Skip same file.\n");
                    goto Next;
                }

                if (file.extension() != L".jxr") {
                    fprintf(stderr, "Not new jxr.\n");
                    goto Next;
                }

                HANDLE file_handle = INVALID_HANDLE_VALUE;
                for (int i = 0; i < 40; ++i) {
                    file_handle =
                        CreateFileW(file.c_str(), FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file_handle == INVALID_HANDLE_VALUE) {
                        auto error = GetLastError();
                        if (error == ERROR_SHARING_VIOLATION) {
                            fprintf(stderr, "Waiting save finishing...\n");
                            Sleep(500);
                        } else {
                            fprintf(stderr, "Bad file: can't open: %lu.\n", error);
                            goto Next;
                        }
                    } else {
                        break;
                    }
                }

                if (file_handle == INVALID_HANDLE_VALUE) {
                    fprintf(stderr, "Bad file: saving wait timed out.\n");
                    goto Next;
                }

                LARGE_INTEGER file_size {};
                if (!GetFileSizeEx(file_handle, &file_size) || file_size.QuadPart <= 0) {
                    CloseHandle(file_handle);
                    fprintf(stderr, "Bad file: not ready for read.\n");
                    goto Next;
                }

                CloseHandle(file_handle);
                if (handle_image(C, file) >= 0) {
                    last = std::move(file);
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
