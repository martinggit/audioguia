#pragma once
#include "domain/Models.h"

namespace audioguia {

class ConfigRepository {
public:
    explicit ConfigRepository(std::filesystem::path portable_root);
    AppConfig load() const;
    void save(const AppConfig& config) const;
    std::filesystem::path file_path() const;
    const std::filesystem::path& root() const noexcept { return root_; }
private:
    std::filesystem::path root_;
};

} // namespace audioguia
