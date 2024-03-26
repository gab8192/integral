#include "search.h"
#include "move_gen.h"
#include "transpo.h"
#include "move_orderer.h"
#include "time_mgmt.h"

#include <iomanip>
#include <format>

Search::Search(TimeManagement::Config &time_config, Board &board)
    : board_(board),
      time_mgmt_(time_config, board),
      branching_factor_(0.0),
      total_bfs_(0),
      can_do_null_move_(true) {}

std::array<std::array<int, 512>, Search::kMaxSearchDepth + 1> Search::kLateMoveReductionTable;

void Search::init_tables() {
  const double kBaseReduction = 0.77;
  const double kDivisor = 2.36;

  for (int depth = 1; depth <= kMaxSearchDepth; depth++) {
    for (int move = 1; move < 512; move++) {
      Search::kLateMoveReductionTable[depth][move] =
          static_cast<int>(kBaseReduction + log(depth) + log(move) / kDivisor);
    }
  }
}

int Search::quiesce(int ply, int alpha, int beta) {
  auto &state = board_.get_state();

  if (board_.is_draw())
    return eval::kDrawScore;

  int static_eval = eval::evaluate(state);
  if (static_eval >= beta || ply >= kMaxGameMoves)
    return static_eval;

  // delta pruning
  if (static_eval + eval::kPieceValues[PieceType::kQueen] < alpha)
    return alpha;

  alpha = std::max(alpha, static_eval);

  int best_score = static_eval;

  MoveOrderer move_orderer(board_, generate_capture_moves(board_), MoveType::kCaptures);
  for (int i = 0; i < move_orderer.size(); i++) {
    if (time_mgmt_.times_up()) {
      return 0;
    }

    const auto &move = move_orderer.get_move(i);

    board_.make_move(move);
    // since the move generator is pseudo-legal, we must verify legality here
    if (king_in_check(flip_color(state.turn), state)) {
      board_.undo_move();
      continue;
    }

    const int score = -quiesce(ply + 1, -beta, -alpha);
    board_.undo_move();

    if (score > best_score) {
      best_score = score;

      if (score >= beta) {
        MoveOrderer::update_killer_move(move, ply);
        break;
      }

      alpha = std::max(alpha, score);
    }
  }

  return best_score;
}

int Search::search(int depth, int ply, int alpha, int beta, PVLine &pv_line) {
  if (board_.is_draw()) {
    return eval::kDrawScore;
  }

  const bool in_pv_node = (beta - alpha) > 1;

  const auto &state = board_.get_state();
  auto &transpo = board_.get_transpo_table();

  const int original_alpha = alpha;
  const auto &tt_entry = transpo.probe(state.zobrist_key);
  if (!in_pv_node && tt_entry.key == state.zobrist_key && tt_entry.depth >= depth) {
    const auto corrected_tt_eval = transpo.correct_score(tt_entry.score, ply);
    switch (tt_entry.flag) {
      case TranspositionTable::Entry::kExact:
        return corrected_tt_eval;
      case TranspositionTable::Entry::kLowerBound:alpha = std::max(alpha, corrected_tt_eval);
        break;
      case TranspositionTable::Entry::kUpperBound:beta = std::min(beta, corrected_tt_eval);
        break;
    }

    if (alpha >= beta) {
      return corrected_tt_eval;
    }
  }

  int extensions = 0;

  // ensure we never run quiesce when in check
  const bool in_check = king_in_check(state.turn, state);
  if (in_check) {
    extensions++;
  }

  // search until we've found a "quiet" position to evaluate
  if (depth <= 0) {
    return quiesce(ply, alpha, beta);
  }

  // reverse (static) futility pruning
  const int kReverseFutilityDepthLimit = 6;
  if (depth <= kReverseFutilityDepthLimit && !in_pv_node && !in_check) {
    const int kMarginIncrement = 120;
    const int kBaseMargin = 100;

    const int futility_margin = kBaseMargin + depth * kMarginIncrement;
    const int static_eval = eval::evaluate(state);
    if (static_eval - futility_margin >= beta) {
      return static_eval;
    }
  }

  // null move pruning
  if (can_do_null_move_ && depth > 2 && !in_check && !in_pv_node) {
    // possible zugwang detection
    for (int color = Color::kBlack; color <= Color::kWhite; color++) {
      for (int piece = PieceType::kKnight; piece <= PieceType::kQueen; piece++) {
        const auto piece_bb = state.piece_bbs[piece] & state.side_bbs[color];
        if (piece_bb.pop_count()) {
          goto move_loop;
        }
      }
    }

    can_do_null_move_ = false;
    board_.make_null_move();

    PVLine dummy_pv;

    const int reduction = depth / 4 + 3;
    const int null_move_score = -search(depth - reduction, ply + 1, -beta, -alpha, dummy_pv);

    board_.undo_move();
    can_do_null_move_ = true;

    if (null_move_score >= beta) {
      return null_move_score > eval::kMateScore - kMaxGameMoves ? beta : null_move_score;
    }
  }
  can_do_null_move_ = true;

  move_loop:
  MoveList quiet_non_cutoffs;
  int moves_tried = 0;

  Move best_move = Move::null_move();
  int best_score = std::numeric_limits<int>::min();
  PVLine temp_pv_line;

  MoveOrderer move_orderer(board_, generate_moves(board_), MoveType::kAll);
  for (int i = 0; i < move_orderer.size(); i++) {
    const Move &move = move_orderer.get_move(i);

    const bool is_capture = move.is_capture(state);
    const bool is_promotion = move.get_promotion_type() != PromotionType::kNone;

    board_.make_move(move);

    // since the move generator is pseudo-legal, we must verify legality here
    if (king_in_check(flip_color(state.turn), state)) {
      board_.undo_move();
      continue;
    }

    const bool is_quiet = !is_capture && !is_promotion;

    PVLine child_pv_line;
    int score;

    // apply LMR conditions to subsequent moves
    int reduction = 0;
    if (depth >= 2 && moves_tried > 1 && !is_promotion && !is_capture && !in_check) {
      reduction = kLateMoveReductionTable[depth][moves_tried];
    }

    // pvs: search the first move with a normal window
    if (moves_tried == 0) {
      score = -search(depth - 1 - reduction, ply + 1, -beta, -alpha, child_pv_line);
    } else {
      // null window search for a quick refutation or indication of a potentially good move
      score = -search(depth - 1 - reduction, ply + 1, -alpha - 1, -alpha, child_pv_line);

      // if the move looks promising from null window search, research
      if (score > alpha && (in_pv_node || reduction > 0)) {
        score = -search(depth - 1 + extensions, ply + 1, -beta, -alpha, child_pv_line);
      }
    }

    board_.undo_move();

    time_mgmt_.update_nodes_searched();
    moves_tried++;

    if (time_mgmt_.times_up() && !best_move.is_null()) {
      temp_pv_line.clear();
      return 0;
    }

    // alpha is raised, therefore this move is the new pv node for this depth
    if (score > best_score) {
      best_score = score;
      best_move = move;

      temp_pv_line.clear();
      temp_pv_line.push(move);

      for (int child_pv_move = 0; child_pv_move < child_pv_line.length(); child_pv_move++) {
        temp_pv_line.push(child_pv_line[child_pv_move]);
      }
    }

    alpha = std::max(alpha, best_score);

    // this opponent has a better move, so we prune this branch
    if (alpha >= beta) {
      if (is_quiet) {
        MoveOrderer::update_killer_move(move, depth);
        MoveOrderer::update_move_history(move, quiet_non_cutoffs, state.turn, depth);
        MoveOrderer::update_counter_move_history(state.move_played, move);
      }
      break;
    }
    else if (is_quiet) {
      quiet_non_cutoffs.push(move);
    }
  }

  pv_line = temp_pv_line;

  // the game is over if we couldn't try a move
  if (moves_tried == 0) {
    return in_check ? -eval::kMateScore + ply : eval::kDrawScore;
  }

  TranspositionTable::Entry entry;
  entry.key = state.zobrist_key;
  entry.score = best_score;
  entry.depth = depth;
  entry.move = best_move;

  if (best_score <= original_alpha) {
    entry.flag = TranspositionTable::Entry::kUpperBound;
  } else if (best_score >= beta) {
    entry.flag = TranspositionTable::Entry::kLowerBound;
  } else {
    entry.flag = TranspositionTable::Entry::kExact;
  }

  transpo.save(entry, ply);
  return best_score;
}

Search::Result Search::search_root(int depth, int ply, int alpha, int beta) {
  Search::Result result;

  const auto &state = board_.get_state();
  const bool in_check = king_in_check(state.turn, state);

  int moves_tried = 0;
  PVLine temp_pv_line;

  MoveList quiet_non_cutoffs;

  MoveOrderer move_orderer(board_, generate_moves(board_), MoveType::kAll);
  for (int i = 0; i < move_orderer.size(); i++) {
    const Move &move = move_orderer.get_move(i);

    const bool is_capture = move.is_capture(state);
    const bool is_promotion = move.get_promotion_type() != PromotionType::kNone;

    board_.make_move(move);

    // since the move generator is pseudo-legal, we must verify legality here
    if (king_in_check(flip_color(state.turn), state)) {
      board_.undo_move();
      continue;
    }

    const bool is_quiet = !is_capture && !is_promotion;

    time_mgmt_.update_nodes_searched();
    const auto prev_nodes_searched = time_mgmt_.get_nodes_searched();

    PVLine child_pv_line;
    int score;

    // apply LMR conditions to subsequent moves
    int reduction = 0;
    if (depth >= 2 && moves_tried > 4 && !is_promotion && !is_capture && !in_check) {
      reduction = kLateMoveReductionTable[depth][moves_tried];
    }

    // pvs: search the first move with a normal window
    if (moves_tried == 0) {
      score = -search(depth - 1 - reduction, ply + 1, -beta, -alpha, child_pv_line);
    } else {
      // null window search for a quick refutation or indication of a potentially good move
      score = -search(depth - 1 - reduction, ply + 1, -alpha - 1, -alpha, child_pv_line);

      // if the move looks promising from null window search, research
      if (score > alpha && (in_pv_node || reduction > 0)) {
        score = -search(depth - 1, ply + 1, -beta, -alpha, child_pv_line);
      }
    }

    board_.undo_move();
    moves_tried++;

    time_mgmt_.update_node_spent_table(move, prev_nodes_searched);
    if (time_mgmt_.times_up() && !result.best_move.is_null()) {
      break;
    }

    // alpha is raised, therefore this move is the new pv node for this depth
    if (score > result.score) {
      result.score = score;
      result.best_move = move;

      temp_pv_line.clear();
      temp_pv_line.push(move);

      for (int child_pv_move = 0; child_pv_move < child_pv_line.length(); child_pv_move++)
        temp_pv_line.push(child_pv_line[child_pv_move]);
    }

    alpha = std::max(alpha, result.score);

    // this opponent has a better move, so we prune this branch
    if (alpha >= beta) {
      if (is_quiet) {
        MoveOrderer::update_killer_move(move, depth);
        MoveOrderer::update_move_history(move, quiet_non_cutoffs, state.turn, depth);
        MoveOrderer::update_counter_move_history(state.move_played, move);
      }
      break;
    }
    else if (is_quiet) {
      quiet_non_cutoffs.push(move);
    }
  }

  result.pv_line = temp_pv_line;

  // the game is over if we couldn't try a move
  if (moves_tried == 0) {
    result.score = in_check ? -eval::kMateScore + ply : eval::kDrawScore;
  }

  return result;
}

Search::Result Search::iterative_deepening() {
  Search::Result result;

  MoveOrderer::reset_move_history();

  const int config_depth = time_mgmt_.get_config().depth;
  const int max_search_depth = config_depth ? config_depth : kMaxSearchDepth;

  for (int depth = 1; depth <= max_search_depth; depth++) {
    can_do_null_move_ = true;

    const int kAspirationWindow = 75;
    const int kAspirationMinDepth = 4;

    int alpha = depth >= kAspirationMinDepth ? result.score - kAspirationWindow : -std::numeric_limits<int>::max();
    int beta = depth >= kAspirationMinDepth ? result.score + kAspirationWindow : std::numeric_limits<int>::max();

    auto new_result = search_root(depth, 0, alpha, beta);
    if (new_result.score <= alpha || new_result.score >= beta) {
      alpha = -std::numeric_limits<int>::max();
      beta = std::numeric_limits<int>::max();
      new_result = search_root(depth, 0, alpha, beta);
    }

    if (!new_result.best_move.is_null()) {
      result = new_result;
    }

    const bool is_mate = eval::is_mate_score(result.score);
    std::cout << std::format("info depth {} score {} {} nodes {} nps {} time {} seldepth {} pv {}",
                             depth,
                             is_mate ? "mate" : "cp",
                             is_mate ? eval::mate_in(result.score) : result.score,
                             time_mgmt_.get_nodes_searched(),
                             static_cast<int>(time_mgmt_.get_nodes_searched() / std::max(1.0, time_mgmt_.time_elapsed() / 1000.0)),
                             time_mgmt_.time_elapsed(),
                             result.pv_line.length(),
                             result.pv_line.to_string()) << std::endl;

    if (time_mgmt_.times_up() || time_mgmt_.root_times_up(result.best_move)) {
      break;
    }
  }

  return result;
}

Search::Result Search::go() {
  time_mgmt_.start();
  const auto result = iterative_deepening();
  time_mgmt_.stop();
  return result;
}