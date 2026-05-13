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

    def choose_move(self, board, my_color, strong_available, time_left):
        """
        board: 2D list, "."=空, "b"=黑一般, "w"=白一般, "B"=黑Strong, "W"=白Strong
        my_color: int, 1=黑, 2=白
        strong_available: int, 持有強子數
        time_left: float, 剩餘時間
        """
        # 1. 轉換盤面為 Compact String
        board_size = len(board)
        compact_board = "".join(["".join(row) for row in board])
                
        # 2. 呼叫 C++ 引擎
        try:
            # 根據剩餘時間調整搜尋深度
            depth = "2"
            if time_left > 60: depth = "3"
            if time_left < 15: depth = "1"
            
            # 參數: <size> <compact_board> <my_color> <strong_available> <depth>
            result = subprocess.run(
                [self.engine_path, str(board_size), compact_board, str(my_color), str(strong_available), depth],
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
