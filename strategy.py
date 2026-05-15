import random
import subprocess
import os

class BotStrategy:
    def __init__(self, username):
        self.username = username
        # 指定 C++ 執行檔路徑 (需先編譯 brain.cpp)
        self.engine_path = "./brain" 
        
        if not os.path.exists(self.engine_path):
            print(f"[Warning] C++ Engine not found at {self.engine_path}! Please compile brain.cpp first.")

    def choose_move(self, board, my_color, strong_available, opp_strong_available, time_left, move_count):
        """
        move_count: 當前是第幾手
        """
        board_size = len(board)
        compact_board = "".join(["".join(row) for row in board])
        
        # 判斷強子是否即將過期 (每 7 手補給一次，在 6, 13, 20... 手前)
        # 如果當前是 5, 12, 19... 手，且手上有強子，這回合不下一回合就浪費了
        is_expiring = 1 if (strong_available > 0 and (move_count % 7 == 5)) else 0
                
        try:
            # 增量引擎極快，基礎深度從 8 開始
            depth = "8"
            # 留 2 秒作為 Python 與系統緩衝
            search_time_ms = int(max(500, (time_left * 1000) * 0.5)) 
            if time_left > 120: 
                depth = "14"
                search_time_ms = 8000 # 初始幾步多思考，建立優勢
            elif time_left > 60: 
                depth = "12"
                search_time_ms = 5000
            elif time_left < 20:
                depth = "8"
                search_time_ms = 1000

            # 參數: <size> <compact_board> <my_color> <strong_available> <opp_strong_available> <is_expiring> <max_depth> <time_ms>
            result = subprocess.run(
                [self.engine_path, str(board_size), compact_board, str(my_color), 
                 str(strong_available), str(opp_strong_available), str(is_expiring), depth, str(search_time_ms)],
                capture_output=True, text=True, check=True, timeout=max(1.0, time_left - 1.0)
            )
            
            # 解析輸出：row col strong
            parts = result.stdout.strip().split()
            if len(parts) >= 3:
                r, c, s = int(parts[0]), int(parts[1]), bool(int(parts[2]))
                return r, c, s
                
        except Exception as e:
            print(f"[AI Error] C++ Engine failed or timed out: {e}")

        # --- Fallback: 隨機落子 ---
        empty_spots = []
        for r in range(board_size):
            for c in range(board_size):
                if board[r][c] == ".":
                    empty_spots.append((r, c))
        if empty_spots:
            r, c = random.choice(empty_spots)
            return r, c, False
        return 0, 0, False

    def choose_bid(self, my_max_bid, default_color):
        """
        Armageddon 競標策略
        """
        # 簡單策略：出 70% 的時間，偏好黑色
        my_bid = my_max_bid * 0.7
        chosen_color = "black"
        return my_bid, chosen_color
