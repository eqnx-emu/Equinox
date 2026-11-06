#pragma once

#include <cstdint>

// Borealis-free decision for the capacity test's delayed stage retry: the
// callback may only start a stream if it is still the retry that was
// scheduled (same generation and stage) and the view is alive, not cancelled
// and not closing.
struct StageRetryTicket {
    uint32_t generation;
    unsigned stage;
};

inline bool should_run_stage_retry(const StageRetryTicket& scheduled,
                                   const StageRetryTicket& current,
                                   bool viewAlive, bool cancelled,
                                   bool closing) {
    return viewAlive && !cancelled && !closing &&
           scheduled.generation == current.generation &&
           scheduled.stage == current.stage;
}
