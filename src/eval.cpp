#include "eval.h"
#include "move_gen.h"

namespace eval {

const std::array<std::array<int, 64>, PieceType::kNumPieceTypes> kPieceSquareTables = {{
    { // pawns
      0, 0, 0, 0, 0, 0, 0, 0,
      50, 50, 50, 50, 50, 50, 50, 50,
      10, 10, 20, 30, 30, 20, 10, 10,
      5, 5, 10, 25, 25, 10, 5, 5,
      0, 0, 0, 20, 20, 0, 0, 0,
      5, -5, -10, 0, 0, -10, -5, 5,
      5, 10, 10, -20, -20, 10, 10, 5,
      0, 0, 0, 0, 0, 0, 0, 0
    },
    { // knights
      -50, -40, -30, -30, -30, -30, -40, -50,
      -40, -20, 0, 0, 0, 0, -20, -40,
      -30, 0, 10, 15, 15, 10, 0, -30,
      -30, 5, 15, 20, 20, 15, 5, -30,
      -30, 0, 15, 20, 20, 15, 0, -30,
      -30, 5, 10, 15, 15, 10, 5, -30,
      -40, -20, 0, 5, 5, 0, -20, -40,
      -50, -40, -30, -30, -30, -30, -40, -50,
    },
    { // bishops
      -20, -10, -10, -10, -10, -10, -10, -20,
      -10, 0, 0, 0, 0, 0, 0, -10,
      -10, 0, 5, 10, 10, 5, 0, -10,
      -10, 5, 5, 10, 10, 5, 5, -10,
      -10, 0, 10, 10, 10, 10, 0, -10,
      -10, 10, 10, 10, 10, 10, 10, -10,
      -10, 5, 0, 0, 0, 0, 5, -10,
      -20, -10, -10, -10, -10, -10, -10, -20,
    },
    { // rooks
      0, 0, 0, 0, 0, 0, 0, 0,
      5, 10, 10, 10, 10, 10, 10, 5,
      -5, 0, 0, 0, 0, 0, 0, -5,
      -5, 0, 0, 0, 0, 0, 0, -5,
      -5, 0, 0, 0, 0, 0, 0, -5,
      -5, 0, 0, 0, 0, 0, 0, -5,
      -5, 0, 0, 0, 0, 0, 0, -5,
      0, 0, 0, 5, 5, 0, 0, 0
    },
    { // queens
      -20, -10, -10, -5, -5, -10, -10, -20,
      -10, 0, 0, 0, 0, 0, 0, -10,
      -10, 0, 5, 5, 5, 5, 0, -10,
      -5, 0, 5, 5, 5, 5, 0, -5,
      0, 0, 5, 5, 5, 5, 0, -5,
      -10, 5, 5, 5, 5, 5, 0, -10,
      -10, 0, 5, 0, 0, 0, 0, -10,
      -20, -10, -10, -5, -5, -10, -10, -20
    },
    { // king middle game
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -20, -30, -30, -40, -40, -30, -30, -20,
      -10, -20, -20, -20, -20, -20, -20, -10,
      20, 20, 0, 0, 0, 0, 20, 20,
      20, 30, 10, 0, 0, 10, 30, 20
    },
}};

const std::array<int, 64> kKingEndgameScores = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10, 0, 0, -10, -20, -30,
    -30, -10, 20, 30, 30, 20, -10, -30,
    -30, -10, 30, 40, 40, 30, -10, -30,
    -30, -10, 30, 40, 40, 30, -10, -30,
    -30, -10, 20, 30, 30, 20, -10, -30,
    -30, -30, 0, 0, 0, 0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};

bool is_mate_score(int evaluation) {
  const int kThreshold = 1048;
  return kMateScore - abs(evaluation) <= kThreshold;
}

int mate_in(int evaluation) {
  if (evaluation > 0 && evaluation < kMateScore) { // mate in favor
    return (kMateScore - evaluation + 1) / 2;
  } else if (evaluation < 0 && evaluation > -kMateScore) { // mate against
    return (kMateScore + evaluation) / 2;
  }

  // not a mate score
  return evaluation;
}

// idea: instead of returning true/false have an end game factor that is some interpolation of the material and game phase
//       this would be valuable for variable evaluation bonuses/penalties
bool is_end_game(const BoardState &state) {
  const int white_material = state.pieces[Color::kWhite][kPawns].pop_count() * eval::kPieceValues[PieceType::kPawn]
      + state.pieces[Color::kWhite][kKnights].pop_count() * eval::kPieceValues[PieceType::kKnight] +
      + state.pieces[Color::kWhite][kBishops].pop_count() * eval::kPieceValues[PieceType::kBishop]
      + state.pieces[Color::kWhite][kQueens].pop_count() * eval::kPieceValues[PieceType::kQueen];
  const int black_material = state.pieces[Color::kBlack][kPawns].pop_count() * eval::kPieceValues[PieceType::kPawn]
      + state.pieces[Color::kBlack][kKnights].pop_count() * eval::kPieceValues[PieceType::kKnight] +
      + state.pieces[Color::kBlack][kBishops].pop_count() * eval::kPieceValues[PieceType::kBishop]
      + state.pieces[Color::kBlack][kQueens].pop_count() * eval::kPieceValues[PieceType::kQueen];

  return white_material + black_material <= 1600;
}

int material_difference(const BoardState &state) {
  int material = 0;

  material += state.pieces[Color::kWhite][kPawns].pop_count() * kPieceValues[PieceType::kPawn];
  material += state.pieces[Color::kWhite][kKnights].pop_count() * kPieceValues[PieceType::kKnight];
  material += state.pieces[Color::kWhite][kBishops].pop_count() * kPieceValues[PieceType::kBishop];
  material += state.pieces[Color::kWhite][kRooks].pop_count() * kPieceValues[PieceType::kRook];
  material += state.pieces[Color::kWhite][kQueens].pop_count() * kPieceValues[PieceType::kQueen];

  material -= state.pieces[Color::kBlack][kPawns].pop_count() * kPieceValues[PieceType::kPawn];
  material -= state.pieces[Color::kBlack][kKnights].pop_count() * kPieceValues[PieceType::kKnight];
  material -= state.pieces[Color::kBlack][kBishops].pop_count() * kPieceValues[PieceType::kBishop];
  material -= state.pieces[Color::kBlack][kRooks].pop_count() * kPieceValues[PieceType::kRook];
  material -= state.pieces[Color::kBlack][kQueens].pop_count() * kPieceValues[PieceType::kQueen];

  return state.turn == Color::kWhite ? material : -material;
}

int positional_difference(int material_diff, const BoardState &state) {
  int position_value = 0;

  const bool is_white = state.turn == Color::kWhite;
  const bool end_game = is_end_game(state);

  if (end_game) {
    const int king_pos = state.pieces[state.turn][kKings].get_lsb_pos();
    position_value += kKingEndgameScores[is_white ? king_pos ^ 56 : king_pos];
  }

  for (int i = 0; i < kPieceSquareTables.size(); i++) {
    BitBoard white_pieces = state.pieces[Color::kWhite][i];
    while (white_pieces) {
      position_value += kPieceSquareTables[i][white_pieces.pop_lsb() ^ 56];
    }

    BitBoard black_pieces = state.pieces[Color::kBlack][i];
    while (black_pieces) {
      position_value -= kPieceSquareTables[i][black_pieces.pop_lsb()];
    }
  }

  return state.turn == Color::kWhite ? position_value : -position_value;
}

int stacked_pawns_difference(const BoardState &state) {
  int stacked_pawns = 0;

  for (const auto& file_mask : kFileMasks) {
    const BitBoard white_pawns_on_file = state.pieces[Color::kWhite][kPawns] & file_mask;
    const BitBoard black_pawns_on_file = state.pieces[Color::kBlack][kPawns] & file_mask;
    stacked_pawns += (white_pawns_on_file.pop_count() > 1) - (black_pawns_on_file.pop_count() > 1);
  }

  const int kStackedPawnPenalty = -12;
  const int penalty = stacked_pawns * kStackedPawnPenalty;

  return state.turn == Color::kWhite ? penalty : -penalty;
}

int passed_pawns_score(const BoardState &state) {
  if (!is_end_game(state))
    return 0;

  int passed_pawns = 0;
  int rooks_behind_passers = 0;

  const BitBoard &white_pawns = state.pieces[Color::kWhite][kPawns];
  const BitBoard &white_rooks = state.pieces[Color::kWhite][kRooks];
  const BitBoard &black_pawns = state.pieces[Color::kBlack][kPawns];
  const BitBoard &black_rooks = state.pieces[Color::kBlack][kRooks];

  for (const auto& file_mask : kFileMasks) {
    const BitBoard black_pawns_on_file = black_pawns & file_mask;
    const BitBoard white_pawns_on_file = white_pawns & file_mask;

    if (black_pawns_on_file == 0 && white_pawns_on_file) {
      passed_pawns++;

      const BitBoard white_rooks_on_file = white_rooks & file_mask;
      if (white_rooks_on_file && white_pawns_on_file.get_lsb_pos() / kBoardRanks >= 5) {
        if (white_pawns_on_file.get_lsb_pos() < white_rooks_on_file.get_msb_pos()) {
          rooks_behind_passers++;
        }
      }
    } else if (white_pawns_on_file == 0 && black_pawns_on_file) {
      passed_pawns--;

      const BitBoard black_rooks_on_file = black_rooks & file_mask;
      if (black_rooks_on_file && black_pawns_on_file.get_lsb_pos() / kBoardRanks <= 4) {
        if (black_pawns_on_file.get_msb_pos() < black_rooks_on_file.get_lsb_pos()) {
          rooks_behind_passers++;
        }
      }
    }
  }

  const int kPassedPawnBonus = 30;
  const int kRooksBehindPassersBonus = 5;
  const int score = passed_pawns * kPassedPawnBonus + rooks_behind_passers * kRooksBehindPassersBonus;

  return state.turn == Color::kWhite ? score : -score;
}

int mobility_difference(const BoardState &state) {
  int mobility = 0;

  const BitBoard &all_pieces = state.occupied();
  const BitBoard &white_pawns = state.pieces[Color::kWhite][kPawns];
  const BitBoard &white_rooks = state.pieces[Color::kWhite][kRooks];
  const BitBoard &black_pawns = state.pieces[Color::kBlack][kPawns];
  const BitBoard &black_rooks = state.pieces[Color::kBlack][kRooks];

  const int kOpenFileBonus = 20;
  const int kSemiOpenFileBonus = 15;

  BitBoard white_rook = white_rooks;
  while (white_rook != 0ULL) {
    U8 rook_file = white_rook.pop_lsb() % kBoardFiles;

    auto pieces_on_file = all_pieces & kFileMasks[rook_file];
    if (pieces_on_file != 0) {
      // rook is on an open file, so it gains 3/4 of a pawn worth
      if (pieces_on_file == (white_rooks & kFileMasks[rook_file])) {
        mobility += kOpenFileBonus;
      }
      // rook is on a semi-open file (a file with only opposing pawns), so it gains half a pawn worth
      else if (pieces_on_file == (black_pawns & kFileMasks[rook_file])) {
        mobility += kSemiOpenFileBonus;
      }
    }
  }

  BitBoard black_rook = black_rooks;
  while (black_rook != 0ULL) {
    U8 rook_file = black_rook.pop_lsb() % kBoardFiles;

    auto pieces_on_file = all_pieces & kFileMasks[rook_file];
    if (pieces_on_file != 0) {
      // rook is on an open file, so it gains 3/4 of a pawn worth
      if (pieces_on_file == (black_rooks & kFileMasks[rook_file])) {
        mobility -= kOpenFileBonus;
      }
      // rook is on a semi-open file (a file with only opposing pawns), so it gains half a pawn worth
      else if (pieces_on_file == (white_pawns & kFileMasks[rook_file])){
        mobility -= kSemiOpenFileBonus;
      }
    }
  }

  return state.turn == Color::kWhite ? mobility : -mobility;
}

int king_safety_difference(const BoardState &state) {
  int score = 0;

  const BitBoard &white_pawns = state.pieces[Color::kWhite][kPawns];
  const BitBoard &white_king = state.pieces[Color::kWhite][kKings];
  const BitBoard &black_pawns = state.pieces[Color::kBlack][kPawns];
  const BitBoard &black_king = state.pieces[Color::kBlack][kKings];

  const int kPawnProtectionBonus = 5;
  const int kDoublePawnProtectionBonus = 4;

  const BitBoard white_left_protection = shift<Direction::kNorthWest>(white_king);
  const BitBoard white_right_protection = shift<Direction::kNorthEast>(white_king);
  const BitBoard white_front_protection = shift<Direction::kNorth>(white_king);

  BitBoard white_protection_squares = white_left_protection | white_front_protection | white_right_protection;
  score += kPawnProtectionBonus * (white_protection_squares & white_pawns).pop_count();

  white_protection_squares = shift<Direction::kNorth>(white_protection_squares);
  score += kDoublePawnProtectionBonus * (white_protection_squares & white_pawns).pop_count();

  const BitBoard black_left_protection = shift<Direction::kSouthWest>(black_king);
  const BitBoard black_right_protection = shift<Direction::kSouthEast>(black_king);
  const BitBoard black_front_protection = shift<Direction::kSouth>(black_king);

  BitBoard black_protection_squares = black_left_protection | black_front_protection | black_right_protection;
  score -= kPawnProtectionBonus * (black_protection_squares & black_pawns).pop_count();

  black_protection_squares = shift<Direction::kSouth>(black_protection_squares);
  score -= kDoublePawnProtectionBonus * (black_protection_squares & black_pawns).pop_count();

  return state.turn == Color::kWhite ? score : -score;
}

int square_control_difference(const BoardState &state) {
  return get_attacked_squares(state, state.turn).pop_count() - get_attacked_squares(state, flip_color(state.turn)).pop_count();
}

int evaluate(const BoardState &state) {
  const int material_diff = material_difference(state);
  const int position_value = positional_difference(material_diff, state);
  const int stacked_pawns = stacked_pawns_difference(state);
  const int mobility = mobility_difference(state);
  // const int passed_pawns = passed_pawns_score(state);
  const int king_safety = king_safety_difference(state);
  const int square_control = square_control_difference(state);

  return material_diff + position_value + stacked_pawns + mobility + king_safety + square_control;
}

}