#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "quantforge/types.hpp"

namespace qf {

/// Interns symbol strings to compact integer ids. Lookups in the hot event
/// path then compare integers instead of strings. Not thread-safe for
/// concurrent writes; populate it up front (single thread) before running a
/// backtest, after which read access is safe from multiple threads.
class SymbolTable {
  public:
    /// Returns the id for `name`, assigning a new one if unseen.
    SymbolId intern(std::string_view name) {
        if (auto it = index_.find(std::string(name)); it != index_.end()) {
            return it->second;
        }
        const auto id = static_cast<SymbolId>(names_.size());
        names_.emplace_back(name);
        index_.emplace(names_.back(), id);
        return id;
    }

    /// Returns the id for `name` or kInvalidSymbol if not present.
    SymbolId lookup(std::string_view name) const {
        if (auto it = index_.find(std::string(name)); it != index_.end()) {
            return it->second;
        }
        return kInvalidSymbol;
    }

    /// Returns the string name for a previously interned id.
    const std::string& name(SymbolId id) const { return names_.at(id); }

    std::size_t size() const noexcept { return names_.size(); }

  private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, SymbolId> index_;
};

}  // namespace qf
