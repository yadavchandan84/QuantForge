#include "quantforge/event.hpp"

#include <variant>

namespace qf {

const char* eventTypeName(const Event& e) noexcept {
    struct Visitor {
        const char* operator()(const MarketEvent&) const noexcept { return "MarketEvent"; }
        const char* operator()(const SignalEvent&) const noexcept { return "SignalEvent"; }
        const char* operator()(const OrderEvent&) const noexcept { return "OrderEvent"; }
        const char* operator()(const FillEvent&) const noexcept { return "FillEvent"; }
    };
    return std::visit(Visitor{}, e.payload);
}

}  // namespace qf
