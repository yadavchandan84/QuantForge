#pragma once

#include <optional>
#include <queue>
#include <vector>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// A deterministic, time-ordered priority queue of events.
///
/// Events are ordered by the tuple (timestamp, priority, sequence):
///   1. timestamp  - earlier events first (the simulation clock never moves
///                   backward on pop);
///   2. priority   - at the same instant, Market < Signal < Order < Fill, so a
///                   strategy always sees data before reacting to it (this is
///                   the structural no-lookahead guarantee);
///   3. sequence   - a monotonically increasing insertion counter breaks any
///                   remaining ties, making the ordering total and the whole
///                   simulation reproducible regardless of insertion pattern.
class EventQueue {
  public:
    /// Pushes an event, stamping it with the next sequence number.
    void push(Event ev) {
        ev.seq = next_seq_++;
        heap_.push(std::move(ev));
    }

    /// Convenience overloads for pushing a typed payload directly.
    template <typename T>
    void emit(T payload) {
        push(Event(std::move(payload)));
    }

    /// Returns and removes the next event in order, or nullopt if empty.
    std::optional<Event> pop() {
        if (heap_.empty()) {
            return std::nullopt;
        }
        // top() is const; move out via a copy of the underlying element.
        Event ev = heap_.top();
        heap_.pop();
        return ev;
    }

    const Event& peek() const { return heap_.top(); }

    bool empty() const noexcept { return heap_.empty(); }
    std::size_t size() const noexcept { return heap_.size(); }

    /// Resets the queue and the sequence counter to a pristine state.
    void clear() {
        heap_ = Heap{};
        next_seq_ = 0;
    }

  private:
    struct Compare {
        /// std::priority_queue is a max-heap, so this returns true when `a`
        /// should come out *after* `b` (i.e. `a` is "greater" / lower priority).
        bool operator()(const Event& a, const Event& b) const noexcept {
            if (a.ts != b.ts) {
                return a.ts > b.ts;
            }
            if (a.priority != b.priority) {
                return a.priority > b.priority;
            }
            return a.seq > b.seq;
        }
    };

    using Heap = std::priority_queue<Event, std::vector<Event>, Compare>;

    Heap heap_;
    SeqNum next_seq_{0};
};

}  // namespace qf
