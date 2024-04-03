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

// Code for calculating NNUE evaluation function

#include "nnue_misc.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iosfwd>
#include <iostream>
#include <sstream>
#include <string_view>

#include "../evaluate.h"
#include "../position.h"
#include "../types.h"
#include "../uci.h"
#include "network.h"
#include "nnue_accumulator.h"

namespace Stockfish::Eval::NNUE {


constexpr std::string_view PieceToChar(" PNBRQK  pnbrqk");


void hint_common_parent_position(const Position& pos, const Networks& networks) {

    int simpleEvalAbs = std::abs(simple_eval(pos, pos.side_to_move()));
    if (simpleEvalAbs > Eval::SmallNetThreshold)
        networks.small.hint_common_access(pos, simpleEvalAbs > Eval::PsqtOnlyThreshold);
    else
        networks.big.hint_common_access(pos, false);
}

namespace {
// Converts a Value into (centi)pawns and writes it in a buffer.
// The buffer must have capacity for at least 5 chars.
void format_cp_compact(Value v, char* buffer, const Position& pos) {

    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');

    int cp = std::abs(UCI::to_cp(v, pos));
    if (cp >= 10000)
    {
        buffer[1] = '0' + cp / 10000;
        cp %= 10000;
        buffer[2] = '0' + cp / 1000;
        cp %= 1000;
        buffer[3] = '0' + cp / 100;
        buffer[4] = ' ';
    }
    else if (cp >= 1000)
    {
        buffer[1] = '0' + cp / 1000;
        cp %= 1000;
        buffer[2] = '0' + cp / 100;
        cp %= 100;
        buffer[3] = '.';
        buffer[4] = '0' + cp / 10;
    }
    else
    {
        buffer[1] = '0' + cp / 100;
        cp %= 100;
        buffer[2] = '.';
        buffer[3] = '0' + cp / 10;
        cp %= 10;
        buffer[4] = '0' + cp / 1;
    }
}


// Converts a Value into pawns, always keeping two decimals
void format_cp_aligned_dot(Value v, std::stringstream& stream, const Position& pos) {

    const double pawns = std::abs(0.01 * UCI::to_cp(v, pos));

    stream << (v < 0   ? '-'
               : v > 0 ? '+'
                       : ' ')
           << std::setiosflags(std::ios::fixed) << std::setw(6) << std::setprecision(2) << pawns;
}
}


// Returns a string with the value of each piece on a board,
// and a table for (PSQT, Layers) values bucket by bucket.
std::string trace(Position& pos, const Eval::NNUE::Networks& networks) {

    std::stringstream ss;

    char board[3 * 8 + 1][8 * 8 + 2];
    std::memset(board, ' ', sizeof(board));

    board[0][8 * 8 + 1]  = '\0';
    board[1][8 * 8 + 1]  = '\0';
    board[2][8 * 8 + 1]  = '\0';
    board[3][8 * 8 + 1]  = '\0';
    board[4][8 * 8 + 1]  = '\0';
    board[5][8 * 8 + 1]  = '\0';
    board[6][8 * 8 + 1]  = '\0';
    board[7][8 * 8 + 1]  = '\0';
    board[8][8 * 8 + 1]  = '\0';
    board[9][8 * 8 + 1]  = '\0';
    board[10][8 * 8 + 1] = '\0';
    board[11][8 * 8 + 1] = '\0';
    board[12][8 * 8 + 1] = '\0';
    board[13][8 * 8 + 1] = '\0';
    board[14][8 * 8 + 1] = '\0';
    board[15][8 * 8 + 1] = '\0';
    board[16][8 * 8 + 1] = '\0';
    board[17][8 * 8 + 1] = '\0';
    board[18][8 * 8 + 1] = '\0';
    board[19][8 * 8 + 1] = '\0';
    board[20][8 * 8 + 1] = '\0';
    board[21][8 * 8 + 1] = '\0';
    board[22][8 * 8 + 1] = '\0';
    board[23][8 * 8 + 1] = '\0';
    board[24][8 * 8 + 1] = '\0';

    // A lambda to output one box of the board
    auto writeSquare = [&board, &pos](File file, Rank rank, Piece pc, Value value) {
        const int x = int(file) * 8;
        const int y = (7 - int(rank)) * 3;

        board[y][x + 1] = board[y + 3][x + 1] = '-';
        board[y][x + 2] = board[y + 3][x + 2] = '-';
        board[y][x + 3] = board[y + 3][x + 3] = '-';
        board[y][x + 4] = board[y + 3][x + 4] = '-';
        board[y][x + 5] = board[y + 3][x + 5] = '-';
        board[y][x + 6] = board[y + 3][x + 6] = '-';
        board[y][x + 7] = board[y + 3][x + 7] = '-';

        board[y + 1][x] = board[y + 1][x + 8] = '|';
        board[y + 2][x] = board[y + 2][x + 8] = '|';


        board[y][x] = board[y][x + 8] = board[y + 3][x + 8] = board[y + 3][x] = '+';
        if (pc != NO_PIECE)
            board[y + 1][x + 4] = PieceToChar[pc];
        if (value != VALUE_NONE)
            format_cp_compact(value, &board[y + 2][x + 2], pos);
    };

    // We estimate the value of each piece by doing a differential evaluation from
    // the current base eval, simulating the removal of the piece from its square.
    Value base = networks.big.evaluate(pos);
    base       = pos.side_to_move() == WHITE ? base : -base;

    for (File f = FILE_A; f <= FILE_H; ++f)
        for (Rank r = RANK_1; r <= RANK_8; ++r)
        {
            Square sq = make_square(f, r);
            Piece  pc = pos.piece_on(sq);
            Value  v  = VALUE_NONE;

            if (pc != NO_PIECE && type_of(pc) != KING)
            {
                auto st = pos.state();

                pos.remove_piece(sq);
                st->accumulatorBig.computed[WHITE]       = st->accumulatorBig.computed[BLACK] =
                  st->accumulatorBig.computedPSQT[WHITE] = st->accumulatorBig.computedPSQT[BLACK] =
                    false;

                Value eval = networks.big.evaluate(pos);
                eval       = pos.side_to_move() == WHITE ? eval : -eval;
                v          = base - eval;

                pos.put_piece(pc, sq);
                st->accumulatorBig.computed[WHITE]       = st->accumulatorBig.computed[BLACK] =
                  st->accumulatorBig.computedPSQT[WHITE] = st->accumulatorBig.computedPSQT[BLACK] =
                    false;
            }

            writeSquare(f, r, pc, v);
        }

    ss << " NNUE derived piece values:\n";
    // for (int row = 0; row < 3 * 8 + 1; ++row)
    //     ss << board[row] << '\n';
    ss << board[0] << '\n';
    ss << board[1] << '\n';
    ss << board[2] << '\n';
    ss << board[3] << '\n';
    ss << board[4] << '\n';
    ss << board[5] << '\n';
    ss << board[6] << '\n';
    ss << board[7] << '\n';
    ss << board[8] << '\n';
    ss << board[9] << '\n';
    ss << board[10] << '\n';
    ss << board[11] << '\n';
    ss << board[12] << '\n';
    ss << board[13] << '\n';
    ss << board[14] << '\n';
    ss << board[15] << '\n';
    ss << board[16] << '\n';
    ss << board[17] << '\n';
    ss << board[18] << '\n';
    ss << board[19] << '\n';
    ss << board[20] << '\n';
    ss << board[21] << '\n';
    ss << board[22] << '\n';
    ss << board[23] << '\n';
    ss << board[24] << '\n';

    ss << '\n';

    auto t = networks.big.trace_evaluate(pos);

    ss << " NNUE network contributions "
       << (pos.side_to_move() == WHITE ? "(White to move)" : "(Black to move)") << std::endl
       << "+------------+------------+------------+------------+\n"
       << "|   Bucket   |  Material  | Positional |   Total    |\n"
       << "|            |   (PSQT)   |  (Layers)  |            |\n"
       << "+------------+------------+------------+------------+\n";

    for (std::size_t bucket = 0; bucket < LayerStacks; ++bucket)
    {
        ss << "|  " << bucket << "        ";
        ss << " |  ";
        format_cp_aligned_dot(t.psqt[bucket], ss, pos);
        ss << "  " << " |  ";
        format_cp_aligned_dot(t.positional[bucket], ss, pos);
        ss << "  " << " |  ";
        format_cp_aligned_dot(t.psqt[bucket] + t.positional[bucket], ss, pos);
        ss << "  " << " |";
        if (bucket == t.correctBucket)
            ss << " <-- this bucket is used";
        ss << '\n';
    }

    ss << "+------------+------------+------------+------------+\n";

    return ss.str();
}


}  // namespace Stockfish::Eval::NNUE
