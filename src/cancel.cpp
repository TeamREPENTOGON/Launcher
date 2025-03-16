#include <atomic>

#include "launcher/cancel.h"

namespace Launcher {
    static std::atomic<bool> _cancelRequested = false;

    void RequestCancel() {
        _cancelRequested.store(true, std::memory_order_release);
    }

    bool CancelRequested() {
        return _cancelRequested.load(std::memory_order_acquire);
    }
}