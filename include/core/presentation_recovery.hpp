#pragma once
#include <new>

namespace wowee::core {

enum class PresentationUpdateResult { Complete, Retry, EndSession };

// Only disposable presentation work belongs here. Authoritative commands and
// simulation must remain outside this boundary: they cannot be blindly retried.
template<class Update, class Reclaim>
PresentationUpdateResult updatePresentationWithRecovery(
        unsigned& consecutiveFailures, Update&& update, Reclaim&& reclaim) {
    try {
        update();
        consecutiveFailures = 0;
        return PresentationUpdateResult::Complete;
    } catch (const std::bad_alloc&) {
        constexpr unsigned maxFailures = 30;
        if (consecutiveFailures < maxFailures) ++consecutiveFailures;
        reclaim();
        return consecutiveFailures >= maxFailures
            ? PresentationUpdateResult::EndSession : PresentationUpdateResult::Retry;
    }
}

} // namespace wowee::core
