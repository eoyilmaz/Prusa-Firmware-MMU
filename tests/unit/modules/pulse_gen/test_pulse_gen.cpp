#include "catch2/catch.hpp"
#include "pulse_gen.h"
#include "../pins.h"
#include <stdio.h>

using Catch::Matchers::Equals;
using namespace modules::pulse_gen;
using hal::gpio::Level;
using hal::gpio::ReadPin;
using hal::tmc2130::MotorParams;

namespace hal {
namespace shr16 {
extern uint8_t shr16_tmc_dir;
} // namespace shr16
} // namespace hal

// Conveniently read the direction set into the lower-level shift register
bool getTMCDir(const MotorParams &mp) {
    return (hal::shr16::shr16_tmc_dir & (1 << mp.idx)) ^ mp.dirOn;
}

// Perform Step() until the move is completed, returning the number of steps performed.
// Ensure the move doesn't run forever, making the test fail reliably.
ssize_t stepUntilDone(PulseGen &pg, const MotorParams &mp, size_t maxSteps = 100000) {
    for (size_t i = 0; i != maxSteps; ++i)
        if (!pg.Step(mp))
            return i;

    // number of steps exceeded
    return -1;
}

TEST_CASE("pulse_gen::basic", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    PulseGen pg(10, 100);

    // perform a simple move
    REQUIRE(pg.Position() == 0);
    pg.PlanMoveTo(10, 1);
    REQUIRE(stepUntilDone(pg, mp) == 10);
    REQUIRE(pg.Position() == 10);

    // return to zero
    pg.PlanMoveTo(0, 1);
    REQUIRE(stepUntilDone(pg, mp) == 10);
    REQUIRE(pg.Position() == 0);

    // don't move
    pg.PlanMoveTo(0, 1);
    REQUIRE(stepUntilDone(pg, mp) == 0);
    REQUIRE(pg.Position() == 0);
}

TEST_CASE("pulse_gen::step_dir", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    PulseGen pg(10, 100);

    // perform a forward move
    REQUIRE(pg.Position() == 0);
    pg.PlanMoveTo(10, 10);
    REQUIRE(stepUntilDone(pg, mp) == 10);
    REQUIRE(pg.Position() == 10);

    // check underlying driver direction
    REQUIRE(getTMCDir(mp));

    // move in reverse
    pg.PlanMoveTo(0, 10);
    REQUIRE(stepUntilDone(pg, mp) == 10);
    REQUIRE(pg.Position() == 0);

    // check underlying driver direction
    REQUIRE(!getTMCDir(mp));

    // forward again (should match initial state)
    pg.PlanMoveTo(5, 10);
    CHECK(stepUntilDone(pg, mp) == 5);
    REQUIRE(getTMCDir(mp));
}

TEST_CASE("pulse_gen::step_count", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    PulseGen pg(10, 100);

    // step manually, ensuring each step is accounted for
    REQUIRE(pg.Position() == 0);
    pg.PlanMoveTo(10, 10);
    bool st = ReadPin(IDLER_STEP_PIN) == Level::high;
    for (size_t i = 0; i != 10; ++i) {
        REQUIRE(pg.Step(mp) > 0);
        bool newSt = ReadPin(IDLER_STEP_PIN) == Level::high;

        // Assuming DEDGE each step should toggle the pin
        REQUIRE(newSt != st);
        st = newSt;
    }

    // there should be one extra timer event to ensure smooth
    // transition between multiple blocks
    REQUIRE(pg.Step(mp) == 0);

    // no pin or position change
    REQUIRE(st == (ReadPin(IDLER_STEP_PIN) == Level::high));
    REQUIRE(pg.Position() == 10);
}

TEST_CASE("pulse_gen::queue_size", "[pulse_gen]") {
    PulseGen pg(10, 100);

    // queue should start empty
    REQUIRE(pg.QueueEmpty());

    // queue the first move
    CHECK(pg.PlanMoveTo(10, 1));
    REQUIRE(!pg.QueueEmpty());

    // queue a second move to fill the queue
    CHECK(pg.PlanMoveTo(15, 1));
    REQUIRE(pg.Full());

    // further enqueuing should fail
    REQUIRE(!pg.PlanMoveTo(20, 1));
}

TEST_CASE("pulse_gen::queue_dropsegments", "[pulse_gen]") {
    PulseGen pg(10, 100);

    // queue should start empty
    REQUIRE(pg.QueueEmpty());
    REQUIRE(pg.Position() == 0);

    // ensure we can enqueue a zero-lenght move successfully
    REQUIRE(pg.PlanMoveTo(0, 1));

    // however the move shouldn't result in a block entry
    REQUIRE(pg.QueueEmpty());
}

TEST_CASE("pulse_gen::queue_step", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    PulseGen pg(10, 100);

    // queue should start empty
    REQUIRE(pg.QueueEmpty());

    // enqueue two moves
    REQUIRE(pg.PlanMoveTo(15, 1));
    REQUIRE(pg.PlanMoveTo(5, 1));

    // check for a total lenght of 25 steps (15+(15-5))
    REQUIRE(stepUntilDone(pg, mp) == 25);

    // check final position
    REQUIRE(pg.Position() == 5);
}

TEST_CASE("pulse_gen::queue_abort", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    PulseGen pg(10, 100);

    // queue should start empty
    REQUIRE(pg.QueueEmpty());

    // enqueue a move and step halfway through
    REQUIRE(pg.PlanMoveTo(10, 1));
    REQUIRE(stepUntilDone(pg, mp, 5) == -1);

    // abort the queue
    pg.AbortPlannedMoves();
    REQUIRE(pg.QueueEmpty());

    // step shouldn't perform extra moves and return a zero timer
    bool st = ReadPin(IDLER_STEP_PIN) == Level::high;
    REQUIRE(pg.Step(mp) == 0);
    REQUIRE(st == (ReadPin(IDLER_STEP_PIN) == Level::high));
    REQUIRE(pg.Position() == 5);
}

TEST_CASE("pulse_gen::accel_ramp", "[pulse_gen]") {
    MotorParams mp = {
        .idx = 0,
        .dirOn = config::idler.dirOn,
        .csPin = IDLER_CS_PIN,
        .stepPin = IDLER_STEP_PIN,
        .sgPin = IDLER_SG_PIN,
        .uSteps = config::idler.uSteps
    };

    // TODO: output ramps still to be checked
    for (int accel = 100; accel <= 5000; accel *= 2) {
        PulseGen pg(10, accel);
        pg.PlanMoveTo(100000, 10000);

        unsigned long ts = 0;
        st_timer_t next;
        do {
            next = pg.Step(mp);
            printf("%lu %u\n", ts, next);
            ts += next;
        } while (next);

        printf("\n\n");
    }
}
