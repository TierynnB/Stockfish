/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "search.h"
#include "ucioption.h"

namespace Stockfish {

int mtg_1  = 50;
int mtg_2  = 50;
int mtg_3  = 50;
int mtg_4  = 50;
int mtg_5  = 50;
int mtg_6  = 50;
int mtg_7  = 50;
int mtg_8  = 50;
int mtg_9  = 50;
int mtg_10 = 50;
int mtg_11 = 50;
int mtg_12 = 50;
int mtg_13 = 50;
int mtg_14 = 50;
int mtg_15 = 50;

TUNE(SetRange(0, 100),
     mtg_1,
     mtg_2,
     mtg_3,
     mtg_4,
     mtg_5,
     mtg_6,
     mtg_7,
     mtg_8,
     mtg_9,
     mtg_10,
     mtg_11,
     mtg_12,
     mtg_13,
     mtg_14,
     mtg_15);

TimePoint TimeManagement::optimum() const { return optimumTime; }
TimePoint TimeManagement::maximum() const { return maximumTime; }
TimePoint TimeManagement::elapsed(size_t nodes) const {
    return useNodesTime ? TimePoint(nodes) : now() - startTime;
}

void TimeManagement::clear() {
    availableNodes = 0;  // When in 'nodes as time' mode
}

void TimeManagement::advance_nodes_time(std::int64_t nodes) {
    assert(useNodesTime);
    availableNodes += nodes;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. We currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManagement::init(Search::LimitsType& limits,
                          Color               us,
                          int                 ply,
                          const OptionsMap&   options) {
    // If we have no time, no need to initialize TM, except for the start time,
    // which is used by movetime.
    startTime = limits.startTime;
    if (limits.time[us] == 0)
        return;

    TimePoint moveOverhead = TimePoint(options["Move Overhead"]);
    TimePoint npmsec       = TimePoint(options["nodestime"]);

    // optScale is a percentage of available time to use for the current move.
    // maxScale is a multiplier applied to optimumTime.
    double optScale, maxScale;

    // If we have to play in 'nodes as time' mode, then convert from time
    // to nodes, and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (npmsec)
    {
        useNodesTime = true;

        if (!availableNodes)                            // Only once at game start
            availableNodes = npmsec * limits.time[us];  // Time is in msec

        // Convert from milliseconds to nodes
        limits.time[us] = TimePoint(availableNodes);
        limits.inc[us] *= npmsec;
        limits.npmsec = npmsec;
    }

    // estimate moves left if movesToGo is default
    int moveNumber           = (ply % 2 == 0) ? (ply / 2) : std::div(ply, 2).quot + 1;
    int estimatedMoveHorizon = 50;

    if (0 < moveNumber && moveNumber <= 10)
    {
        estimatedMoveHorizon = mtg_1;
    }
    else if (10 < moveNumber && moveNumber <= 20)
    {
        estimatedMoveHorizon = mtg_2;
    }
    else if (20 < moveNumber && moveNumber <= 30)
    {
        estimatedMoveHorizon = mtg_3;
    }
    else if (30 < moveNumber && moveNumber <= 40)
    {
        estimatedMoveHorizon = mtg_4;
    }
    else if (40 < moveNumber && moveNumber <= 50)
    {
        estimatedMoveHorizon = mtg_5;
    }
    else if (50 < moveNumber && moveNumber <= 60)
    {
        estimatedMoveHorizon = mtg_6;
    }
    else if (60 < moveNumber && moveNumber <= 70)
    {
        estimatedMoveHorizon = mtg_7;
    }
    else if (70 < moveNumber && moveNumber <= 80)
    {
        estimatedMoveHorizon = mtg_8;
    }
    else if (80 < moveNumber && moveNumber <= 90)
    {
        estimatedMoveHorizon = mtg_9;
    }
    else if (90 < moveNumber && moveNumber <= 100)
    {
        estimatedMoveHorizon = mtg_10;
    }
    else if (100 < moveNumber && moveNumber <= 110)
    {
        estimatedMoveHorizon = mtg_11;
    }
    else if (110 < moveNumber && moveNumber <= 120)
    {
        estimatedMoveHorizon = mtg_12;
    }
    else if (120 < moveNumber && moveNumber <= 130)
    {
        estimatedMoveHorizon = mtg_13;
    }
    else if (130 < moveNumber && moveNumber <= 140)
    {
        estimatedMoveHorizon = mtg_14;
    }
    else if (140 < moveNumber && moveNumber <= 150)
    {
        estimatedMoveHorizon = mtg_15;
    }


    // Maximum move horizon of 50 moves
    int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : estimatedMoveHorizon;

    // if less than one second, gradually reduce mtg
    if (limits.time[us] < 1000 && (double(mtg) / limits.time[us] > 0.05))
    {
        mtg = limits.time[us] * 0.05;
    }

    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint timeLeft = std::max(TimePoint(1), limits.time[us] + limits.inc[us] * (mtg - 1)
                                                  - moveOverhead * (2 + mtg));

    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    if (limits.movestogo == 0)
    {
        // Use extra time with larger increments
        double optExtra = limits.inc[us] < 500 ? 1.0 : 1.13;

        // Calculate time constants based on current time left.
        double optConstant =
          std::min(0.00308 + 0.000319 * std::log10(limits.time[us] / 1000.0), 0.00506);
        double maxConstant = std::max(3.39 + 3.01 * std::log10(limits.time[us] / 1000.0), 2.93);

        optScale = std::min(0.0122 + std::pow(ply + 2.95, 0.462) * optConstant,
                            0.213 * limits.time[us] / double(timeLeft))
                 * optExtra;
        maxScale = std::min(6.64, maxConstant + ply / 12.0);
    }

    // x moves in y seconds (+ z increment)
    else
    {
        optScale = std::min((0.88 + ply / 116.4) / mtg, 0.88 * limits.time[us] / double(timeLeft));
        maxScale = std::min(6.3, 1.5 + 0.11 * mtg);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optScale * timeLeft);
    maximumTime =
      TimePoint(std::min(0.825 * limits.time[us] - moveOverhead, maxScale * optimumTime)) - 10;

    if (options["Ponder"])
        optimumTime += optimumTime / 4;
}

}  // namespace Stockfish
