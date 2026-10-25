// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/UiFeed.cpp
// =============================================================================
#include "UiFeed.h"

#include <map>
#include <mutex>

namespace omnis::vst3 {

namespace {

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::map<std::uint64_t, std::weak_ptr<UiFeed>>& registry() {
    static std::map<std::uint64_t, std::weak_ptr<UiFeed>> r;
    return r;
}

} // namespace

std::uint64_t registerFeed(std::shared_ptr<UiFeed> feed) {
    std::lock_guard<std::mutex> lock(registryMutex());
    static std::uint64_t next = 0x4f4d4e4953000000ull; // « OMNIS »
    const std::uint64_t token = ++next;
    registry()[token] = feed;
    return token;
}

void unregisterFeed(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().erase(token);
}

std::shared_ptr<UiFeed> findFeed(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(registryMutex());
    const auto it = registry().find(token);
    return it == registry().end() ? nullptr : it->second.lock();
}

} // namespace omnis::vst3
