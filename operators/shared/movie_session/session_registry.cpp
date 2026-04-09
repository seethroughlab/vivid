#include "session_registry.h"

PlaybackSessionRegistry& PlaybackSessionRegistry::instance() {
    static PlaybackSessionRegistry registry;
    return registry;
}

std::shared_ptr<PlaybackSession> PlaybackSessionRegistry::acquire(const std::string& operator_id,
                                                                  const std::string& source_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(operator_id);
    if (it != sessions_.end()) {
        it->second->set_source_path(source_path);
        it->second->acquire();
        return it->second;
    }
    auto session = std::make_shared<PlaybackSession>(operator_id, source_path);
    session->acquire();
    sessions_[operator_id] = session;
    return session;
}

void PlaybackSessionRegistry::release(const std::string& operator_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(operator_id);
    if (it == sessions_.end()) return;
    it->second->release();
    if (it->second->ref_count() <= 0) {
        sessions_.erase(it);
    }
}
