#pragma once

#include <filesystem>

namespace diff_patcher {
    enum PatchFolderResult {
        PATCH_OK,
        PATCH_NO_JSON,
        PATCH_INVALID_JSON,
        PATCH_FAIL
    };

    enum PatchOperation {
        OP_CREATE,
        OP_DELETE,
        OP_PATCH
    };

    struct PatchError {
        std::string name;
        PatchOperation op;
        int code;
    };

    PatchFolderResult PatchFolder(const std::filesystem::path& rootFolderToPatch,
        const std::filesystem::path& rootPatchesFolder,
        std::vector<PatchError>* errors);

    /**
     * Poison the first byte of the main function of the given Isaac executable.
     *
     * This replaces the first byte of the function with an int3 instruction.
     * This prevents people from accidentally running the executable without
     * Repentogon, which could lead to issues with savefiles due to a lack of
     * forward compatibility in the absence of Repentogon.
     *
     * Return true on success, false on error.
     */
    bool PatchIsaacMain(const char* filename);

    /**
     * Signature of main() inside the Isaac executable.
     *
     * Unlike a ZHL signature, this one has no wildcards as the executable is
     * completely static. The syntax used is the binary one as we do not
     * use ZHL's packed representation of signatures.
     *
     * We use a signature instead of an offset as there is no guarantee that
     * steamless and other file modifying tools may not alter the offset when
     * they are used on the executable.
     */
    static constexpr const char ISAAC_MAIN_SIGNATURE[] = "\x55\x8b\xec\x6a\xfe\x68\x18\xf5\xb5\x00\x68\xac"
        "\x74\xa8\x00\x64\xa1\x00\x00\x00\x00\x50\x81\xec\x3c\x05\x00\x00";

    static const char ISAAC_POISON_BYTE = '\xcc';
}