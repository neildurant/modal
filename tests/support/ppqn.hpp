#pragma once

#include <modal/core/tick.hpp>

#include <catch2/generators/catch_generators.hpp>

// Runs the enclosing test case once per resolution: the default, a much finer one, and the
// coarse resolution common in older MIDI files. Use for every test that touches ticks, to
// catch code that quietly assumes the default.
#define MODAL_ANY_PPQN() (::modal::core::Ppqn{GENERATE(::modal::core::kPpqn.value, 6720, 96)})
