#include <algorithm>
#include <bitset>
#include <climits>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>

using namespace std;

/**
 * 高階 Rokumoku AI 引擎 (正式版)
 * 使用技術：Bitboard, Zobrist Hashing, Transposition Table, 全強子模擬
 */

typedef uint64_t HashKey;

struct BitBoard {
  bitset<361> black;
  bitset<361> white;
  bitset<361> strong;
  int size;

  bool is_occupied(int pos) const { return black.test(pos) || white.test(pos); }
  bool can_be_overwritten(int pos) const { return !strong.test(pos); }
};

class RokumokuAI {
private:
  int size;
  int my_color;                  // 1: black, 2: white
  HashKey zobrist_table[361][5]; // 0:empty, 1:b, 2:w, 3:B, 4:W
  struct TTEntry {
    int depth;
    int score;
  };
  unordered_map<HashKey, TTEntry> transposition_table;

  void init_zobrist() {
    mt19937_64 rng(42);
    for (int i = 0; i < 361; ++i)
      for (int j = 0; j < 5; ++j)
        zobrist_table[i][j] = rng();
  }

  struct Patterns {
    int six = 0;
    int live_5 = 0;
    int dead_5 = 0;
    int live_4 = 0;
    int live_3 = 0;
  };

  Patterns count_patterns(const BitBoard &b, int color) {
    Patterns p;
    const bitset<361> &my_b = (color == 1) ? b.black : b.white;
    const bitset<361> &opp_b = (color == 1) ? b.white : b.black;
    bool visited[4][361] = {false};

    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int r = 0; r < size; ++r) {
      for (int c = 0; c < size; ++c) {
        int pos = r * size + c;
        if (!my_b.test(pos)) continue;
        for (int i = 0; i < 4; ++i) {
          if (visited[i][pos]) continue;
          int count = 1;
          visited[i][pos] = true;
          int last_r = r, last_c = c;
          for (int step = 1; step < 6; ++step) {
            int nr = r + dr[i] * step, nc = c + dc[i] * step;
            if (nr >= 0 && nr < size && nc >= 0 && nc < size && my_b.test(nr * size + nc)) {
              count++;
              visited[i][nr * size + nc] = true;
              last_r = nr; last_c = nc;
            } else break;
          }
          int prev_r = r - dr[i], prev_c = c - dc[i];
          int next_r = last_r + dr[i], next_c = last_c + dc[i];
          auto is_empty = [&](int nr, int nc) {
            return nr >= 0 && nr < size && nc >= 0 && nc < size && !b.is_occupied(nr * size + nc);
          };
          bool left_open = is_empty(prev_r, prev_c);
          bool right_open = is_empty(next_r, next_c);

          if (count >= 6) p.six++;
          else if (count == 5) {
            if (left_open && right_open) p.live_5++;
            else if (left_open || right_open) p.dead_5++;
          } else if (count == 4 && left_open && right_open) p.live_4++;
          else if (count == 3 && left_open && right_open) p.live_3++;
        }
      }
    }
    return p;
  }

  // 檢查是否贏了 (局部掃描優化)
  bool is_win_at(const BitBoard &b, int pos, int color) {
    const bitset<361> &my_b = (color == 1) ? b.black : b.white;
    const bitset<361> &opp_b = (color == 1) ? b.white : b.black;
    int r = pos / size, c = pos % size;
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};

    int l5 = 0, l4 = 0, d5 = 0, l3 = 0;
    bool six = false;

    for (int i = 0; i < 4; ++i) {
      int count = 1;
      int left_open = 0, right_open = 0;
      // Forward
      for (int step = 1; step < 6; ++step) {
        int nr = r + dr[i] * step, nc = c + dc[i] * step;
        if (nr >= 0 && nr < size && nc >= 0 && nc < size) {
          if (my_b.test(nr * size + nc)) count++;
          else { if (!opp_b.test(nr * size + nc)) right_open = 1; break; }
        } else break;
      }
      // Backward
      for (int step = 1; step < 6; ++step) {
        int nr = r - dr[i] * step, nc = c - dc[i] * step;
        if (nr >= 0 && nr < size && nc >= 0 && nc < size) {
          if (my_b.test(nr * size + nc)) count++;
          else { if (!opp_b.test(nr * size + nc)) left_open = 1; break; }
        } else break;
      }

      if (count >= 6) six = true;
      if (count == 5) {
        if (left_open && right_open) l5++;
        else if (left_open || right_open) d5++;
      }
      if (count == 4 && left_open && right_open) l4++;
      if (count == 3 && left_open && right_open) l3++;
    }

    if (six || l5 > 0) return true;
    
    // 如果局部沒有達成組合，但有潛力，才做全局掃描
    if (l4 > 0 || d5 > 0 || l3 > 0) {
      Patterns p = count_patterns(b, color);
      return (p.six > 0 || p.live_5 > 0 || p.live_4 >= 2 || (p.live_4 >= 1 && p.dead_5 >= 1) || p.live_3 >= 3);
    }
    return false;
  }

  vector<int> get_ordered_moves(const BitBoard &b, bool has_strong) {
    vector<pair<int, int>> candidates;
    bitset<361> near;
    int dr[] = {-1, -1, -1, 0, 0, 1, 1, 1, -2, -2, -2, -2, -2, 0, 0, 2, 2, 2, 2, 2};
    int dc[] = {-1, 0, 1, -1, 1, -1, 0, 1, -2, -1, 0, 1, 2, -2, 2, -2, -1, 0, 1, 2};

    for (int i = 0; i < size * size; ++i) {
      if (b.black.test(i) || b.white.test(i)) {
        int r = i / size, c = i % size;
        for (int j = 0; j < 20; ++j) {
          int nr = r + dr[j], nc = c + dc[j];
          if (nr >= 0 && nr < size && nc >= 0 && nc < size) {
            int npos = nr * size + nc;
            // 如果手上有強子，考慮對手普通子的位置
            if (!b.is_occupied(npos) || (has_strong && b.can_be_overwritten(npos)))
              near.set(npos);
          }
        }
      }
    }

    if (near.none()) return {size * size / 2};

    for (int i = 0; i < size * size; ++i) {
      if (near.test(i)) {
        int dist_score = (size - abs(i / size - size / 2) - abs(i % size - size / 2));
        // 如果位置上有對手子，給予額外權重以鼓勵評估強子覆蓋
        if (b.is_occupied(i)) dist_score += 10;
        candidates.push_back({dist_score, i});
      }
    }
    sort(candidates.rbegin(), candidates.rend());
    vector<int> res;
    for (auto &p : candidates) res.push_back(p.second);
    return res;
  }

  // 評估函數：基於連子數量的位元掃描
  int evaluate(const BitBoard &b) {
    Patterns my_p = count_patterns(b, my_color);
    Patterns opp_p = count_patterns(b, 3 - my_color);

    if (my_p.six > 0 || my_p.live_5 > 0 || my_p.live_4 >= 2 || (my_p.live_4 >= 1 && my_p.dead_5 >= 1) || my_p.live_3 >= 3)
      return 1000000;
    if (opp_p.six > 0 || opp_p.live_5 > 0 || opp_p.live_4 >= 2 || (opp_p.live_4 >= 1 && opp_p.dead_5 >= 1) || opp_p.live_3 >= 3)
      return -1000000;

    int score = 0;
    score += my_p.dead_5 * 5000 + my_p.live_4 * 1000 + my_p.live_3 * 200;
    score -= (opp_p.dead_5 * 5000 + opp_p.live_4 * 1000 + opp_p.live_3 * 200);

    for (int i = 0; i < size * size; ++i) {
      if (b.black.test(i) && my_color == 1) score += (size - abs(i / size - size / 2) - abs(i % size - size / 2));
      if (b.white.test(i) && my_color == 2) score += (size - abs(i / size - size / 2) - abs(i % size - size / 2));
    }
    return score;
  }

  int alpha_beta(BitBoard &b, int depth, int alpha, int beta, bool is_max,
                 int my_s, int opp_s, HashKey h) {
    if (transposition_table.count(h) && transposition_table[h].depth >= depth)
      return transposition_table[h].score;

    if (depth == 0) return evaluate(b);

    int best = is_max ? INT_MIN : INT_MAX;
    int cur_color = is_max ? my_color : 3 - my_color;
    bitset<361> &cur_side = (cur_color == 1) ? b.black : b.white;
    bitset<361> &opp_side = (cur_color == 1) ? b.white : b.black;
    int cur_s = is_max ? my_s : opp_s;

    vector<int> moves = get_ordered_moves(b, cur_s > 0);

    for (int i : moves) {
      // 1. 普通落子
      if (!b.is_occupied(i)) {
        cur_side.set(i);
        if (is_win_at(b, i, cur_color)) {
          cur_side.reset(i);
          return is_max ? (1000000 + depth) : (-1000000 - depth);
        }
        int val = alpha_beta(b, depth - 1, alpha, beta, !is_max, my_s, opp_s,
                             h ^ zobrist_table[i][cur_color]);
        cur_side.reset(i);
        if (is_max) { best = max(best, val); alpha = max(alpha, val); }
        else { best = min(best, val); beta = min(beta, val); }
        if (beta <= alpha) break;
      }

      // 2. 強子覆蓋 (模擬中也嘗試強子)
      if (cur_s > 0 && b.can_be_overwritten(i)) {
        bool was_opp = opp_side.test(i);
        bool was_my = cur_side.test(i);
        if (was_opp) opp_side.reset(i);
        if (was_my) cur_side.reset(i);

        cur_side.set(i); b.strong.set(i);
        if (is_win_at(b, i, cur_color)) {
          b.strong.reset(i); cur_side.reset(i);
          if (was_opp) opp_side.set(i);
          if (was_my) cur_side.set(i);
          return is_max ? (1000000 + depth) : (-1000000 - depth);
        }
        int val = alpha_beta(b, depth - 1, alpha, beta, !is_max, is_max ? my_s - 1 : my_s,
                             !is_max ? opp_s - 1 : opp_s, h ^ zobrist_table[i][cur_color + 2]);
        
        // 模擬中對進攻型強子給予獎勵
        if (was_opp) val += (is_max ? 30 : -30);

        b.strong.reset(i); cur_side.reset(i);
        if (was_opp) opp_side.set(i);
        if (was_my) cur_side.set(i);

        if (is_max) { best = max(best, val); alpha = max(alpha, val); }
        else { best = min(best, val); beta = min(beta, val); }
        if (beta <= alpha) break;
      }
    }
    transposition_table[h] = {depth, best};
    return best;
  }

public:
  RokumokuAI(int s, int color) : size(s), my_color(color) { init_zobrist(); }

  void run(const string &compact, int my_s, int opp_s, int depth) {
    BitBoard b;
    b.size = size;
    HashKey h = 0;
    for (int i = 0; i < size * size; ++i) {
      if (compact[i] == 'b') { b.black.set(i); h ^= zobrist_table[i][1]; }
      else if (compact[i] == 'B') { b.black.set(i); b.strong.set(i); h ^= zobrist_table[i][3]; }
      else if (compact[i] == 'w') { b.white.set(i); h ^= zobrist_table[i][2]; }
      else if (compact[i] == 'W') { b.white.set(i); b.strong.set(i); h ^= zobrist_table[i][4]; }
    }

    int best_val = -2000000;
    int br = size / 2, bc = size / 2;
    bool b_strong = false;

    vector<int> moves = get_ordered_moves(b, my_s > 0);

    // 第一層搜尋：嘗試所有可能的合法動作
    for (int i : moves) {
      // 1. 嘗試普通落子
      bitset<361> &my_set = (my_color == 1 ? b.black : b.white);
      if (!b.is_occupied(i)) {
        my_set.set(i);
        int val;
        if (is_win_at(b, i, my_color)) val = 1500000;
        else val = alpha_beta(b, depth - 1, -2000000, 2000000, false, my_s, opp_s, h ^ zobrist_table[i][my_color]);
        my_set.reset(i);

        if (val > best_val) { best_val = val; br = i / size; bc = i % size; b_strong = false; }
      }

      // 2. 嘗試強子覆蓋 (目標是空位或對手普通子)
      if (my_s > 0 && b.can_be_overwritten(i)) {
        bitset<361> &opp_set = (my_color == 1 ? b.white : b.black);
        bool was_opp = opp_set.test(i);
        bool was_my = my_set.test(i);
        
        if (was_opp) opp_set.reset(i);
        if (was_my) my_set.reset(i);

        my_set.set(i);
        b.strong.set(i);
        int val;
        if (is_win_at(b, i, my_color)) val = 1500000;
        else val = alpha_beta(b, depth - 1, -2000000, 2000000, false, my_s - 1, opp_s, h ^ zobrist_table[i][my_color + 2]);

        // 強子獎勵：如果是覆蓋對手的棋子，給予顯著加分以鼓勵進攻
        if (was_opp) val += 150; 
        // 額外：如果強子能創造出高分模式，再加分
        if (val > 10000) val += 200;

        b.strong.reset(i);
        my_set.reset(i);
        if (was_opp) opp_set.set(i);
        if (was_my) my_set.set(i);

        if (val > best_val) { best_val = val; br = i / size; bc = i % size; b_strong = true; }
      }
    }
    cout << br << " " << bc << " " << (b_strong ? 1 : 0) << endl;
  }
};

int main(int argc, char *argv[]) {
  if (argc < 6)
    return 1;
  int size = stoi(argv[1]);
  int color = stoi(argv[3]);
  int my_s = stoi(argv[4]);
  int depth = stoi(argv[5]);
  RokumokuAI ai(size, color);
  // 傳入盤面, 我方強子, 對方強子(簡化為相同), 深度
  ai.run(argv[2], my_s, my_s, depth);
  return 0;
}
