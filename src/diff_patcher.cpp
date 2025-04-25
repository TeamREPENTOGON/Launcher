#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <bzlib.h>
#include <bspatch.h>
#include <corecrt_io.h>
#include <fcntl.h>
#include <bspatchlib.h>
#include <bsdifflib.c>
#include <compat.h>

namespace fs = std::filesystem;
using namespace rapidjson;
#define BUF_SIZE 4096 //in reality, this came from a copypasted shit, but Im defining it manually because im tired

static off_t offtin(const unsigned char* buf) {
    off_t y = buf[7] & 0x7F;
    for (int i = 6; i >= 0; i--) y = y * 256 + buf[i];
    if (buf[7] & 0x80) y = -y;
    return y;
}

bool bspatch_stream(const char* oldfile, const char* patchfile, const char* newfile) {
    FILE* fold = NULL, * fnew = NULL;
    FILE* fctrl = NULL, * fdiff = NULL, * fextra = NULL;
    BZFILE* ctrl_bz = NULL, * diff_bz = NULL, * extra_bz = NULL;
    int bzerr;
    unsigned char header[HEADER_SIZE];
    off_t bzctrllen, bzdifflen, newsize;
    off_t oldpos = 0, newpos = 0;
    off_t ctrl[3];
    unsigned char buf[BUF_SIZE];
    unsigned char oldbuf[BUF_SIZE];
    bool ok = false;

    try {
    if ((fold = fopen(oldfile, "rb")) == NULL) throw new std::exception("fold fail");
    if ((fctrl = fopen(patchfile, "rb")) == NULL) throw new std::exception("fpatch fail");
    if ((fnew = fopen(newfile, "wb")) == NULL) throw new std::exception("fnew fail");

    if (fread(header, 1, HEADER_SIZE, fctrl) != HEADER_SIZE) throw new std::exception("invalid patch format! (empty?)");
    if (memcmp(header, "BSDIFF40", 8) != 0) throw new std::exception("invalid patch format!");

    bzctrllen = offtin(header + 8);
    bzdifflen = offtin(header + 16);
    newsize = offtin(header + 24);

    if (fseeko(fctrl, HEADER_SIZE, SEEK_SET) != 0) throw new std::exception("corrupted or fucked up patch!");
    ctrl_bz = BZ2_bzReadOpen(&bzerr, fctrl, 0, 0, NULL, 0);
    if (bzerr != BZ_OK) throw new std::exception("corrupted or fucked up patch! (code:2)");

    if ((fdiff = fopen(patchfile, "rb")) == NULL) throw new std::exception("patch file gone?");
    if (fseeko(fdiff, HEADER_SIZE + bzctrllen, SEEK_SET) != 0) throw new std::exception("diff seek fail");
    diff_bz = BZ2_bzReadOpen(&bzerr, fdiff, 0, 0, NULL, 0);
    if (bzerr != BZ_OK) throw new std::exception("bz2 decompression error!");

    if ((fextra = fopen(patchfile, "rb")) == NULL) throw new std::exception("patch file gone? Ex");
    if (fseeko(fextra, HEADER_SIZE + bzctrllen + bzdifflen, SEEK_SET) != 0) throw new std::exception("patch seek fail Ex");
    extra_bz = BZ2_bzReadOpen(&bzerr, fextra, 0, 0, NULL, 0);
    if (bzerr != BZ_OK) throw new std::exception("bz2 decompression error! Ex");

    while (newpos < newsize) {
        for (int i = 0; i < 3; i++) {
            unsigned char cbuf[8];
            int r = BZ2_bzRead(&bzerr, ctrl_bz, cbuf, 8);
            if (r != 8 || (bzerr != BZ_OK && bzerr != BZ_STREAM_END)) throw new std::exception("bz2 decompression error! (Code:2)");
            ctrl[i] = offtin(cbuf);
        }
        if (ctrl[0] < 0 || ctrl[1] < 0) throw new std::exception("corrupt patch or bzdecomperr (code:69)");

        off_t remain = ctrl[0];
        while (remain > 0) {
            int chunk = (int)(remain < BUF_SIZE ? remain : BUF_SIZE);
            int got = BZ2_bzRead(&bzerr, diff_bz, buf, chunk);
            if (got < 0 || (bzerr != BZ_OK && bzerr != BZ_STREAM_END)) throw new std::exception("corrupt patch or bzdecomperr (code:6969)");
            
            fseeko(fold, oldpos, SEEK_SET);
            int oldgot = fread(oldbuf, 1, got, fold);
            for (int j = 0; j < got; j++) {
                buf[j] = buf[j] + (j < oldgot ? oldbuf[j] : 0);
            }
            fwrite(buf, 1, got, fnew);
            oldpos += got;
            newpos += got;
            remain -= got;
        }

        remain = ctrl[1];
        while (remain > 0) {
            int chunk = (int)(remain < BUF_SIZE ? remain : BUF_SIZE);
            int got = BZ2_bzRead(&bzerr, extra_bz, buf, chunk);
            if (got < 0 || (bzerr != BZ_OK && bzerr != BZ_STREAM_END))throw new std::exception("corrupt patch or bzdecomperr (code:696969)");
            fwrite(buf, 1, got, fnew);
            newpos += got;
            remain -= got;
        }
        
        oldpos += ctrl[2];
    }

    ok = true;
    }
    catch (std::exception ex) {}
    if (ctrl_bz) BZ2_bzReadClose(&bzerr, ctrl_bz);
    if (diff_bz) BZ2_bzReadClose(&bzerr, diff_bz);
    if (extra_bz) BZ2_bzReadClose(&bzerr, extra_bz);
    if (fctrl) fclose(fctrl);
    if (fdiff) fclose(fdiff);
    if (fextra) fclose(fextra);
    if (fold) fclose(fold);
    if (fnew) fclose(fnew);
    return ok;
}


bool patchfolder(const fs::path& folder_a, const fs::path& folder_c) {
    fs::path manifest = folder_c / "manifest.json";
    std::ifstream mf(manifest);
    if (!mf) { std::cerr << "[Error] Manifest not found\n"; return false; }
    IStreamWrapper isw(mf);
    Document doc;
    doc.ParseStream(isw);
    for (auto& v : doc["delete"].GetArray()) fs::remove(folder_a / v.GetString());
    for (auto& m : doc["create"].GetObject()) {
        fs::path dst = folder_a / m.name.GetString();
        fs::create_directories(dst.parent_path());
        fs::copy_file(folder_c / m.value.GetString(), dst, fs::copy_options::overwrite_existing);
    }
    for (auto& v : doc["patch"].GetArray()) {
        fs::path p = folder_c / "patches" / (std::string(v.GetString()) + ".patch");
        fs::path o = folder_a / v.GetString();
        fs::path t = o; t += ".patched";
        if (!bspatch_stream(o.string().c_str(), p.string().c_str(), t.string().c_str())) {
            std::cerr << "[Error] Failed to apply patch for " << v.GetString() << '\n';
            return false;
        }
        fs::remove(o);
        fs::rename(t, o);
    }
    return true;
}
