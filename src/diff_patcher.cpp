#include <cstring>
#include <cstdlib>

#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

#include <bzlib.h>
#include <bspatch.h>
#include <bspatchlib.h>
#include <compat.h>

#include "launcher/diff_patcher.h"
#include "shared/logger.h"
#include "shared/pe32.h"
#include "shared/scoped_file.h"

namespace fs = std::filesystem;
using namespace rapidjson;

/* In reality, this came from a copypasted shit, but I'm defining it manually
 * because I'm tired
 */
static constexpr size_t BUF_SIZE = 4096;

/* Taken from bsdifflib.c */
#define HEADER_SIZE 32

class ScopedBZ2 {
public:
    ScopedBZ2(int* error) : ScopedBZ2(error, nullptr) {

    }

    ScopedBZ2(int* error, BZFILE* f) : _error(error), _f(f) {

    }

    ~ScopedBZ2() {
        if (_f)
            BZ2_bzReadClose(_error, _f);
    }

    ScopedBZ2& operator=(BZFILE* f) {
        if (_f)
            BZ2_bzReadClose(_error, _f);

        _f = f;
        return *this;
    }

    operator BZFILE* () const {
        return _f;
    }

    operator bool() const {
        return _f != nullptr;
    }

private:
    BZFILE* _f = nullptr;
    int* _error;
};

/* Assert: buf has at least eight elements.
 * Taken from bspatchlib.c.
 */
static off_t offtin(const unsigned char* buf) {
    off_t y = buf[7] & 0x7F;
    for (int i = 6; i >= 0; i--) y = y * 256 + buf[i];
    if (buf[7] & 0x80) y = -y;
    return y;
}

void bspatch_stream(const char* oldfile, const char* patchfile, const char* newfile) {
    ScopedFile fold, fnew, fctrl, fdiff, fextra;
    int bzerr_ctrl, bzerr_diff, bzerr_extra;
    ScopedBZ2 ctrl_bz(&bzerr_ctrl), diff_bz(&bzerr_diff), extra_bz(&bzerr_extra);
    unsigned char header[HEADER_SIZE];
    off_t bzctrllen, bzdifflen, newsize;
    off_t oldpos = 0, newpos = 0;
    off_t ctrl[3];
    unsigned char buf[BUF_SIZE];
    unsigned char oldbuf[BUF_SIZE];

    if ((fold = fopen(oldfile, "rb")) == NULL) {
        throw std::exception("fold fail");
    }

    if ((fctrl = fopen(patchfile, "rb")) == NULL) {
        throw std::exception("fpatch fail");
    }

    if ((fnew = fopen(newfile, "wb")) == NULL) {
        throw std::exception("fnew fail");
    }

    if (fread(header, 1, HEADER_SIZE, fctrl) != HEADER_SIZE) {
        throw std::exception("invalid patch format! (empty?)");
    }

    if (memcmp(header, "BSDIFF40", 8)) {
        throw std::exception("invalid patch format!");
    }

    bzctrllen = offtin(header + 8);
    bzdifflen = offtin(header + 16);
    newsize = offtin(header + 24);

    if (fseeko(fctrl, HEADER_SIZE, SEEK_SET) != 0) {
        throw std::exception("corrupted or fucked up patch!");
    }

    ctrl_bz = BZ2_bzReadOpen(&bzerr_ctrl, fctrl, 0, 0, NULL, 0);

    if (bzerr_ctrl != BZ_OK) {
        throw std::exception("corrupted or fucked up patch! (code:2)");
    }

    if ((fdiff = fopen(patchfile, "rb")) == NULL) {
        throw std::exception("patch file gone?");
    }

    if (fseeko(fdiff, HEADER_SIZE + bzctrllen, SEEK_SET) != 0) {
        throw std::exception("diff seek fail");
    }

    diff_bz = BZ2_bzReadOpen(&bzerr_diff, fdiff, 0, 0, NULL, 0);

    if (bzerr_diff != BZ_OK) {
        throw std::exception("bz2 decompression error!");
    }

    if ((fextra = fopen(patchfile, "rb")) == NULL) {
        throw std::exception("patch file gone? Ex");
    }

    if (fseeko(fextra, HEADER_SIZE + bzctrllen + bzdifflen, SEEK_SET) != 0) {
        throw std::exception("patch seek fail Ex");
    }

    extra_bz = BZ2_bzReadOpen(&bzerr_extra, fextra, 0, 0, NULL, 0);

    if (bzerr_extra != BZ_OK) {
        throw std::exception("bz2 decompression error! Ex");
    }

    while (newpos < newsize) {
        for (int i = 0; i < 3; i++) {
            unsigned char cbuf[8];
            int r = BZ2_bzRead(&bzerr_ctrl, ctrl_bz, cbuf, 8);
            if (r != 8 || (bzerr_ctrl != BZ_OK && bzerr_ctrl != BZ_STREAM_END)) {
                throw std::exception("bz2 decompression error! (Code:2)");
            }
            ctrl[i] = offtin(cbuf);
        }

        if (ctrl[0] < 0 || ctrl[1] < 0) {
            throw std::exception("corrupt patch or bzdecomperr (code:69)");
        }

        off_t remain = ctrl[0];
        while (remain > 0) {
            int chunk = (int)(remain < BUF_SIZE ? remain : BUF_SIZE);
            int got = BZ2_bzRead(&bzerr_diff, diff_bz, buf, chunk);
            if (got < 0 || (bzerr_diff != BZ_OK && bzerr_diff != BZ_STREAM_END)) {
                throw std::exception("corrupt patch or bzdecomperr (code:6969)");
            }

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
            int got = BZ2_bzRead(&bzerr_extra, extra_bz, buf, chunk);
            if (got < 0 || (bzerr_extra != BZ_OK && bzerr_extra != BZ_STREAM_END)) {
                throw std::exception("corrupt patch or bzdecomperr (code:696969)");
            }
            fwrite(buf, 1, got, fnew);
            newpos += got;
            remain -= got;
        }

        oldpos += ctrl[2];
    }
}

namespace diff_patcher {
    bool PatchIsaacMain(const char* file) {
        try {
            PE32 pe32(file);
            auto [textSectionHeader, textStart] = pe32.GetSection(".text");

            if (!textSectionHeader) {
                Logger::Error("diff_patcher::PatchIsaacMain: executable %s has no .text section\n", file);
                return false;
            }

            PE32Byte mainFn = pe32.Lookup(ISAAC_MAIN_SIGNATURE, textSectionHeader);
            if (!mainFn) {
                Logger::Error("diff_patcher::PatchIsaacMain: main() not found in executable %s\n", file);
                return false;
            }

            if (!pe32.Patch(mainFn, std::string(&ISAAC_POISON_BYTE).c_str())) {
                Logger::Error("diff_patcher::PatchIsaacMain: error while patching main in %s\n", file);
                return false;
            }

            if (!pe32.Write()) {
                Logger::Error("diff_patcher::PatchIsaacMain: error while writing back %s\n", file);
                return false;
            }

            return true;
        } catch (std::runtime_error const& e) {
            Logger::Error("diff_patcher::PatchIsaacMain: error while processing %s as a PE executable\n", file);
            return false;
        }
    }

    static void PushError(std::vector<PatchError>* errors, std::string name,
        PatchOperation op, std::error_code errc) {
        if (errors) {
            PatchError err;
            err.op = op;
            err.name = std::move(name);
            err.code = errc.value();
            errors->push_back(std::move(err));
        }
    }

    PatchFolderResult PatchFolder(const fs::path& rootFolderToPatch,
        const fs::path& rootPatchesFolder,
        std::vector<PatchError>* errors) {
        fs::path manifest = rootPatchesFolder / "manifest.json";

        std::ifstream mf(manifest);
        if (!mf.good()) {
            Logger::Error("PatchFolder: manifest not found\n");
            return PATCH_NO_JSON;
        }

        IStreamWrapper isw(mf);
        Document doc;
        doc.ParseStream(isw);
        if (doc.HasParseError()) {
            Logger::Error("PatchFolder: errors encountered in manifest file: %d:%d\n",
                doc.GetParseError(), doc.GetErrorOffset());
            return PATCH_INVALID_JSON;
        }

        bool ok = true;
        if (doc.HasMember("delete")) {
            std::error_code err;
            for (auto const& v : doc["delete"].GetArray()) {
                std::string name = v.GetString();
                Logger::Info("PatchFolder: Deleting file: %s\n", name.c_str());
                fs::remove(rootFolderToPatch / name, err);
                if (err) {
                    Logger::Error("PatchFolder: unable to delete %s\n", name.c_str());
                    PushError(errors, name, OP_DELETE, err);
                    ok = false;
                }
            }
        }

        if (doc.HasMember("create")) {
            for (auto const& m : doc["create"].GetObject()) {
                fs::path dst = rootFolderToPatch / m.name.GetString();
                std::error_code err;
                fs::path hierarchy = dst.parent_path();
                fs::create_directories(hierarchy, err);

                if (err) {
                    Logger::Error("PatchFolder: unable to create hierarchy %s\n", hierarchy.string().c_str());
                    PushError(errors, hierarchy.string(), OP_CREATE, err);
                    ok = false;
                    continue;
                }

                std::string name = m.value.GetString();
                Logger::Info("PatchFolder: Creating file: %s\n", name.c_str());
                fs::copy_file(rootPatchesFolder / name, dst, fs::copy_options::overwrite_existing, err);
                if (err) {
                    Logger::Error("PatchFolder: unable to create file %s\n", name.c_str());
                    PushError(errors, name, OP_CREATE, err);
                    ok = false;
                }
            }
        }

        if (doc.HasMember("patch")) {
            for (auto& v : doc["patch"].GetArray()) {
                fs::path patch = rootPatchesFolder / "patches" / (std::string(v.GetString()) + ".patch");
                std::string name = v.GetString();
                fs::path original = rootFolderToPatch / name;
                fs::path temporary = original;
                temporary += ".patched";

                Logger::Info("PatchFolder: Patching `%s` using patch file `%s`\n", original.string().c_str(), patch.string().c_str());

                try {
                    std::string s = temporary.string();
                    bspatch_stream(original.string().c_str(), patch.string().c_str(),
                        s.c_str());

                    if (const char* substring = strstr(s.c_str(), "isaac-ng.exe")) {
                        if (!strcmp(substring, "isaac-ng.exe")) {
                            PatchIsaacMain(substring);
                        }
                    }
                } catch (std::exception& e) {
                    Logger::Error("PatchFolder: unable to apply patch for %s (%s)\n", name.c_str(),
                        e.what());
                    std::error_code err;
                    err.assign(-1, std::generic_category());
                    PushError(errors, name, OP_PATCH, err);
                    ok = false;
                }

                if (ok) {
                    if (fs::exists(temporary)) {
                        if (fs::exists(original)) {
                            fs::remove(original);
                        }
                        fs::rename(temporary, original);
                    }
                } else if (fs::exists(temporary)) {
                    fs::remove(temporary);
                }
            }
        }

        return ok ? PATCH_OK : PATCH_FAIL;
    }
}