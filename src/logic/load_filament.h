#pragma once
#include <stdint.h>
#include "command_base.h"
#include "feed_to_finda.h"
#include "feed_to_bondtech.h"

namespace logic {

/// A high-level command state machine
/// Handles the complex logic of loading filament
class LoadFilament : public CommandBase {
public:
    inline LoadFilament()
        : CommandBase() {}

    /// Restart the automaton
    void Reset(uint8_t param) override;

    /// @returns true if the state machine finished its job, false otherwise
    bool Step() override;

private:
    FeedToFinda feed;
    FeedToBondtech james; // bond ;)
};

extern LoadFilament loadFilament;

} // namespace logic