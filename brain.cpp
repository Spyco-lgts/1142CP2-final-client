#include <algorithm>
#include <bitset>
#include <climits>
#include <iostream>
#include <random>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <chrono>

using namespace std;

/**
 * Rokumoku Ultimate Hybrid Engine (Final Version)
 * 合併 V1 的高勝率連續掃描速度與 V2 的跳號識別、時間控制、穩定性
 */

typedef uint64_t HashKey;

struct BitBoard {
  bitset<361> black;
  bitset<361> white;
  bitset<361> strong;
  int size;

  bool is_occupied(int pos) const { return black.test(pos) || white.test(pos); }
  bool can_be_overwritten(int pos) const { return !strong.test(pos); }
  int get_piece(int pos) const {
      if (black.test(pos)) return 1;
      if (white.test(pos)) return 2;
      return 0;
  }
};

struct Patterns {
    int win6 = 0;
    int live5 = 0;
    int dead5 = 0;
    int live4 = 0;
    int dead4 = 0;
    int live3 = 0;
};

class RokumokuAI {
private:
  int size;
  int my_color;
  HashKey zobrist_table[361][5];
  struct TTEntry {
    HashKey key;
    int depth;
    int score;
  };
  unique_ptr<TTEntry[]> tt;
  const uint32_t tt_size = 1 << 20;

  chrono::steady_clock::time_point start_time;
  int64_t time_limit_ms;
  bool stop_search = false;
  int nodes_searched = 0;

  void init_zobrist() {
    mt19937_64 rng(42);
    for (int i = 0; i < 361; ++i)
      for (int j = 0; j < 5; ++j)
        zobrist_table[i][j] = rng();
  }

  bool check_time() {
    if (stop_search) return true;
    if (++nodes_searched % 2048 == 0) {
      auto now = chrono::steady_clock::now();
      auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start_time).count();
      if (elapsed >= time_limit_ms) stop_search = true;
    }
    return stop_search;
  }

  Patterns count_patterns(const BitBoard &b, int color, int s_available) {
    Patterns p;
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
            int pos = r * size + c;
            if (b.get_piece(pos) != color) continue;
            for (int i = 0; i < 4; ++i) {
                int pr = r - dr[i], pc = c - dc[i];
                if (pr >= 0 && pr < size && pc >= 0 && pc < size && b.get_piece(pr * size + pc) == color) continue;
                int line[25], is_s[25], len = 0, cr = r, cc = c;
                while (cr >= 0 && cr < size && cc >= 0 && cc < size && len < 15) {
                    int cp = cr * size + cc;
                    line[len] = b.get_piece(cp);
                    is_s[len] = b.strong.test(cp) ? 1 : 0;
                    len++; cr += dr[i]; cc += dc[i];
                }
                if (len < 6) continue;
                for (int j = 0; j <= len - 6; ++j) {
                    int mine = 0, empty = 0, enemy_normal = 0;
                    for (int k = 0; k < 6; ++k) {
                        if (line[j+k] == color) mine++;
                        else if (line[j+k] == 0) empty++;
                        else if (!is_s[j+k]) enemy_normal++;
                    }
                    if (mine == 6) p.win6++;
                    else if (mine == 5 && (empty + (s_available > 0 ? enemy_normal : 0) == 1)) p.dead5++;
                    else if (mine == 4 && (empty + (s_available > 0 ? enemy_normal : 0) == 2)) p.dead4++;
                }
                for (int j = 0; j <= len - 7; ++j) {
                    if (line[j] == 0 && line[j+6] == 0) {
                        int mine = 0; bool blocked = false;
                        for (int k = 1; k <= 5; ++k) {
                            if (line[j+k] == color) mine++; else if (line[j+k] != 0) blocked = true;
                        }
                        if (!blocked) {
                            if (mine == 5) p.live5++; else if (mine == 4) p.live4++; else if (mine == 3) p.live3++;
                        }
                    }
                }
            }
        }
    }
    return p;
  }

  bool is_win_at(const BitBoard &b, int pos, int color) {
    const bitset<361> &my_b = (color == 1) ? b.black : b.white;
    int r = pos / size, c = pos % size, dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int i = 0; i < 4; ++i) {
      int count = 1;
      for (int step = 1; step < 6; ++step) {
        int nr = r + dr[i]*step, nc = c + dc[i]*step;
        if (nr>=0 && nr<size && nc>=0 && nc<size && my_b.test(nr*size+nc)) count++; else break;
      }
      for (int step = 1; step < 6; ++step) {
        int nr = r - dr[i]*step, nc = c - dc[i]*step;
        if (nr>=0 && nr<size && nc>=0 && nc<size && my_b.test(nr*size+nc)) count++; else break;
      }
      if (count >= 6) return true;
    }
    return false;
  }

  int evaluate(const BitBoard &b, int my_s, int opp_s) {
    Patterns my_p = count_patterns(b, my_color, my_s);
    Patterns opp_p = count_patterns(b, 3 - my_color, opp_s);
    if (my_p.win6 > 0) return 80000000;
    if (opp_p.win6 > 0) return -80000000;
    int score = my_p.live5 * 10000000 - opp_p.live5 * 12000000;
    score += my_p.dead5 * 1000000 - opp_p.dead5 * 1500000;
    score += my_p.live4 * 200000 - opp_p.live4 * 300000;
    score += my_p.live3 * 50000 - opp_p.live3 * 80000;
    for (int i = 0; i < size * size; ++i) if (b.is_occupied(i)) {
        int dist = (size - abs(i / size - size / 2) - abs(i % size - size / 2));
        score += (b.get_piece(i) == my_color ? 10 : -10) * dist;
    }
    return score;
  }

  vector<int> get_ordered_moves(const BitBoard &b, bool has_strong) {
    vector<pair<int, int>> candidates; bitset<361> near;
    int dr[] = {-1, -1, -1, 0, 0, 1, 1, 1, -2, -2, -2, -2, -2, 0, 0, 2, 2, 2, 2, 2};
    int dc[] = {-1, 0, 1, -1, 1, -1, 0, 1, -2, -1, 0, 1, 2, -2, 2, -2, -1, 0, 1, 2};
    for (int i = 0; i < size * size; ++i) if (b.is_occupied(i)) {
        int r = i / size, c = i % size;
        for (int j = 0; j < 20; ++j) {
            int nr = r + dr[j], nc = c + dc[j];
            if (nr >= 0 && nr < size && nc >= 0 && nc < size) {
                int npos = nr * size + nc;
                if (!b.is_occupied(npos) || (has_strong && b.can_be_overwritten(npos))) near.set(npos);
            }
        }
    }
    if (near.none()) return {size * size / 2};
    for (int i = 0; i < size * size; ++i) if (near.test(i)) {
        int dist = (size - abs(i / size - size / 2) - abs(i % size - size / 2));
        if (b.is_occupied(i)) dist += 50; candidates.push_back({dist, i});
    }
    sort(candidates.rbegin(), candidates.rend());
    vector<int> res; for (auto &p : candidates) res.push_back(p.second);
    return res;
  }

  int alpha_beta(BitBoard &b, int depth, int alpha, int beta, bool is_max, int my_s, int opp_s, HashKey h) {
    if (check_time()) return 0;
    uint32_t idx = h & (tt_size - 1);
    if (tt[idx].key == h && tt[idx].depth >= depth) return tt[idx].score;
    if (depth == 0) return evaluate(b, my_s, opp_s);
    int best = is_max ? INT_MIN : INT_MAX;
    int cur_color = is_max ? my_color : 3 - my_color;
    bitset<361> &cur_side = (cur_color == 1) ? b.black : b.white;
    bitset<361> &opp_side = (cur_color == 1) ? b.white : b.black;
    int cur_s = is_max ? my_s : opp_s;
    vector<int> moves = get_ordered_moves(b, cur_s > 0);
    for (int i : moves) {
      if (!b.is_occupied(i)) {
        cur_side.set(i);
        if (is_win_at(b, i, cur_color)) { cur_side.reset(i); return is_max ? (80000000 + depth) : (-80000000 - depth); }
        int val = alpha_beta(b, depth - 1, alpha, beta, !is_max, my_s, opp_s, h ^ zobrist_table[i][cur_color]);
        cur_side.reset(i); if (stop_search) return 0;
        if (is_max) { best = max(best, val); alpha = max(alpha, val); } else { best = min(best, val); beta = min(beta, val); }
        if (beta <= alpha) break;
      }
      if (cur_s > 0 && b.can_be_overwritten(i)) {
        bool was_opp = opp_side.test(i), was_my = cur_side.test(i);
        if (was_opp) opp_side.reset(i); if (was_my) cur_side.reset(i);
        cur_side.set(i); b.strong.set(i);
        if (is_win_at(b, i, cur_color)) { b.strong.reset(i); cur_side.reset(i); if (was_opp) opp_side.set(i); if (was_my) cur_side.set(i); return is_max ? (80000000 + depth) : (-80000000 - depth); }
        int val = alpha_beta(b, depth - 1, alpha, beta, !is_max, is_max ? my_s - 1 : my_s, !is_max ? opp_s - 1 : opp_s, h ^ zobrist_table[i][cur_color + 2]);
        if (was_opp) val += (is_max ? 50 : -50);
        b.strong.reset(i); cur_side.reset(i); if (was_opp) opp_side.set(i); if (was_my) cur_side.set(i);
        if (stop_search) return 0;
        if (is_max) { best = max(best, val); alpha = max(alpha, val); } else { best = min(best, val); beta = min(beta, val); }
        if (beta <= alpha) break;
      }
    }
    tt[idx].key = h; tt[idx].depth = depth; tt[idx].score = best; return best;
  }

public:
  RokumokuAI(int s, int color) : size(s), my_color(color) { 
    init_zobrist(); tt = make_unique<TTEntry[]>(tt_size);
    for(uint32_t i=0; i<tt_size; ++i) tt[i].key = 0;
  }

  void run(const string &compact, int my_s, int opp_s, bool must_use, int max_depth, int64_t limit_ms) {
    start_time = chrono::steady_clock::now(); time_limit_ms = limit_ms;
    BitBoard b; b.size = size; HashKey h = 0;
    for (int i = 0; i < size * size; ++i) {
      if (compact[i] == 'b') { b.black.set(i); h ^= zobrist_table[i][1]; }
      else if (compact[i] == 'B') { b.black.set(i); b.strong.set(i); h ^= zobrist_table[i][3]; }
      else if (compact[i] == 'w') { b.white.set(i); h ^= zobrist_table[i][2]; }
      else if (compact[i] == 'W') { b.white.set(i); b.strong.set(i); h ^= zobrist_table[i][4]; }
    }
    int best_r = size / 2, best_c = size / 2; bool b_strong = false;
    for (int d = 1; d <= max_depth; ++d) {
        int cur_best_val = -100000000, cur_r = best_r, cur_c = best_c; bool cur_s = b_strong;
        vector<int> moves = get_ordered_moves(b, my_s > 0);
        bool depth_complete = true;
        for (int i : moves) {
            bitset<361> &my_set = (my_color == 1 ? b.black : b.white);
            if (!b.is_occupied(i)) {
                my_set.set(i); int v = is_win_at(b, i, my_color) ? 90000000 : alpha_beta(b, d - 1, -100000000, 100000000, false, my_s, opp_s, h ^ zobrist_table[i][my_color]);
                my_set.reset(i); if (stop_search) { depth_complete = false; break; }
                if (must_use && v < 80000000) v -= 5000000;
                if (v > cur_best_val) { cur_best_val = v; cur_r = i/size; cur_c = i%size; cur_s = false; }
            }
            if (my_s > 0 && b.can_be_overwritten(i)) {
                bitset<361> &opp_set = (my_color == 1 ? b.white : b.black); bool was_opp = opp_set.test(i), was_my = my_set.test(i);
                if (was_opp) opp_set.reset(i); if (was_my) my_set.reset(i);
                my_set.set(i); b.strong.set(i); int v = is_win_at(b, i, my_color) ? 90000000 : alpha_beta(b, d - 1, -100000000, 100000000, false, my_s - 1, opp_s, h ^ zobrist_table[i][my_color + 2]);
                b.strong.reset(i); my_set.reset(i); if (was_opp) opp_set.set(i); if (was_my) my_set.set(i);
                if (stop_search) { depth_complete = false; break; }
                if (v > cur_best_val) { cur_best_val = v; cur_r = i/size; cur_c = i%size; cur_s = true; }
            }
        }
        if (depth_complete) { best_r = cur_r; best_c = cur_c; b_strong = cur_s; if (cur_best_val >= 80000000) break; } else break;
    }
    if (b.is_occupied(best_r * size + best_c) && (!b_strong || !b.can_be_overwritten(best_r * size + best_c))) {
        for (int i = 0; i < size * size; ++i) if (!b.is_occupied(i)) { best_r = i/size; best_c = i%size; b_strong = false; break; }
    }
    cout << best_r << " " << best_c << " " << (b_strong ? 1 : 0) << endl;
  }
};

int main(int argc, char *argv[]) {
  if (argc < 9) return 1;
  int size = stoi(argv[1]); string compact = argv[2]; int color = stoi(argv[3]);
  int my_s = stoi(argv[4]), opp_s = stoi(argv[5]); bool must_use = (stoi(argv[6]) == 1);
  int max_depth = stoi(argv[7]); int64_t limit_ms = stoll(argv[8]);
  RokumokuAI ai(size, color); ai.run(compact, my_s, opp_s, must_use, max_depth, limit_ms);
  return 0;
}
