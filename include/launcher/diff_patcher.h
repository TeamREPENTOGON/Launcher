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
}