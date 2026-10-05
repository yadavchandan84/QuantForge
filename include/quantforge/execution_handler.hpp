#pragma once

#include <memory>
#include <optional>
#include <random>

#include "quantforge/event.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// Snapshot of the market for one symbol at the moment an order is executed.
/// Supplied by the engine from the most recent MarketEvent.
struct MarketSnapshot {
    Price reference{0.0};      ///< Mid/close/last used as the base fill price.
    Quantity volume{0.0};      ///< Available volume this bar/tick (for impact).
    Price bid{0.0};
    Price ask{0.0};
};

/// Configuration for the execution handler's fill behavior.
struct ExecutionConfig {
    /// Maximum fraction of available volume a single order may consume in one
    /// execution step. Orders larger than this fill partially. In (0, 1].
    double max_participation{0.1};

    /// If true, when the snapshot has no volume information (volume <= 0), the
    /// order fills completely (useful for daily close-only data). If false, an
    /// order against zero volume does not fill.
    bool fill_on_zero_volume{true};
};

/// Composable execution handler.
///
/// Policy-based: latency, slippage, and fee models are injected and can be
/// mixed freely. Given an order and the current market snapshot it produces a
/// FillEvent (possibly partial). Randomness (random latency) is drawn from a
/// seeded RNG the handler owns, so a fixed seed yields identical fills.
class ExecutionHandler {
  public:
    ExecutionHandler(std::unique_ptr<LatencyModel> latency,
                     std::unique_ptr<SlippageModel> slippage,
                     std::unique_ptr<FeeModel> fee, ExecutionConfig cfg = {},
                     std::uint64_t seed = 0);

    /// Deep-copies the handler (clones policies) and reseeds its RNG. Used by
    /// parameter sweeps so each worker runs an independent, deterministic copy.
    ExecutionHandler clone(std::uint64_t seed) const;

    /// Executes `order` against `snap`. Returns a FillEvent, or nullopt if the
    /// order could not fill at all (e.g. no volume and fill_on_zero_volume is
    /// false, or a non-marketable limit order).
    std::optional<FillEvent> execute(const OrderEvent& order,
                                     const MarketSnapshot& snap);

    /// Resets the RNG to the configured seed (for deterministic replay).
    void reseed(std::uint64_t seed);

    const ExecutionConfig& config() const noexcept { return cfg_; }

  private:
    std::unique_ptr<LatencyModel> latency_;
    std::unique_ptr<SlippageModel> slippage_;
    std::unique_ptr<FeeModel> fee_;
    ExecutionConfig cfg_;
    std::uint64_t seed_;
    std::mt19937_64 rng_;
};

}  // namespace qf
