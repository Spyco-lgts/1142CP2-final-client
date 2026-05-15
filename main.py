import json
import os
import threading
import time
import uuid

import sseclient

from api import ArenaAPI, ArenaAPIError
from strategy import BotStrategy


def load_dotenv(path=".env"):
    if not os.path.exists(path):
        return

    with open(path, encoding="utf-8") as env_file:
        for raw_line in env_file:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export ") :].lstrip()

            key, separator, raw_value = line.partition("=")
            if not separator:
                continue

            key = key.strip()
            if not key or key in os.environ:
                continue

            value = raw_value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
                value = value[1:-1]
            elif " #" in value:
                value = value.split(" #", 1)[0].rstrip()

            os.environ[key] = value


load_dotenv()

API_BASE_URL = os.getenv("ARENA_URL", "http://localhost:8080")
API_KEY = os.getenv("BOT_API_KEY", "your_bot_api_key")
ROOM_ID = os.getenv("ROOM_ID", "room1")
SEAT_PREFERENCE = os.getenv("BOT_SEAT", "black").strip().lower() or "black"

if SEAT_PREFERENCE not in {"black", "white"}:
    SEAT_PREFERENCE = "black"

api = ArenaAPI(API_BASE_URL)


def seat_attempt_order():
    fallback = "white" if SEAT_PREFERENCE == "black" else "black"
    return [SEAT_PREFERENCE, fallback]


def decode_compact_board(board_compact, board_size):
    try:
        size = int(board_size)
    except (TypeError, ValueError):
        size = 15
    if size <= 0:
        size = 15
    text = str(board_compact or "")
    board = []
    for row in range(size):
        row_values = []
        for col in range(size):
            index = row * size + col
            row_values.append(text[index] if index < len(text) else ".")
        board.append(row_values)
    return board


def seated_username(room, seat):
    seat_key = "1" if seat == "black" else "2"
    return room.get("seated_usernames", {}).get(seat_key)


def current_seat(room, username):
    if seated_username(room, "black") == username:
        return "black"
    if seated_username(room, "white") == username:
        return "white"
    if username in room.get("spectators", []):
        return "spectator"
    return None


def find_my_player_id(room, username):
    for player_id, seated_username_value in room.get("player_usernames", {}).items():
        if seated_username_value == username:
            return int(player_id)
    if seated_username(room, "black") == username:
        return 1
    if seated_username(room, "white") == username:
        return 2
    return None


def try_take_seat(room, username):
    if current_seat(room, username) in {"black", "white"}:
        return
    for seat in seat_attempt_order():
        if seated_username(room, seat):
            continue
        try:
            print(f"Attempting to take {seat} seat...")
            api.take_seat(ROOM_ID, seat)
            return
        except ArenaAPIError as exc:
            print(f"[Seat Attempt Failed] {seat}: {exc}")


def heartbeat_loop(presence_id, stop_event):
    while not stop_event.is_set():
        try:
            api.send_heartbeat(ROOM_ID, presence_id)
        except Exception as exc:  # noqa: BLE001
            print(f"[Heartbeat Error] {exc}")
        stop_event.wait(5)


def handle_room_snapshot(room, username, strategy, runtime_state):
    status = room.get("status")
    if status == "finished":
        print("Game finished.")
        return False

    my_player_id = find_my_player_id(room, username)
    my_seat = current_seat(room, username)

    if my_seat not in {"black", "white"}:
        try_take_seat(room, username)
        return True

    if my_player_id is None:
        return True

    ready_info = room.get("ready_info") or {}
    my_ready = ready_info.get("confirmed", {}).get(str(my_player_id))
    ready_key = f"{room.get('current_game_id')}:{my_player_id}"
    if room.get("awaiting_player_confirmation") and not my_ready and runtime_state.get("last_ready_key") != ready_key:
        try:
            print("Sending ready signal...")
            api.ready(ROOM_ID)
            runtime_state["last_ready_key"] = ready_key
        except ArenaAPIError as exc:
            print(f"[Ready Error] {exc}")
    elif my_ready or not room.get("awaiting_player_confirmation"):
        runtime_state["last_ready_key"] = None

    player_colors = room.get("player_colors") or {}
    player_time_left = room.get("player_time_left") or {}
    strong_by_color = room.get("strong_pieces_available") or {}
    board = room.get("board")
    if not isinstance(board, list):
        board = decode_compact_board(room.get("board_compact"), room.get("board_size"))

    my_color = player_colors.get(str(my_player_id))
    opp_color = 3 - int(my_color) if my_color is not None else None
    time_left = float(player_time_left.get(str(my_player_id), 0.0) or 0.0)
    strong_available = int(strong_by_color.get(str(my_color), 0) or 0) if my_color is not None else 0
    opp_strong_available = int(strong_by_color.get(str(opp_color), 0) or 0) if opp_color is not None else 0

    turn_info = room.get("turn_info")
    move_count = room.get("move_count") or 0
    if room.get("awaiting_move") and turn_info and turn_info.get("player_id") == my_player_id:
        move_key = f"{room.get('current_game_id')}:{move_count}:{my_player_id}"
        if runtime_state.get("last_move_key") == move_key:
            return True
        try:
            print(f"My turn! Color={my_color}, Time left={time_left:.2f}s, Move={move_count}")
            row, col, use_strong = strategy.choose_move(
                board=board,
                my_color=my_color,
                strong_available=strong_available,
                opp_strong_available=opp_strong_available,
                time_left=time_left,
                move_count=move_count,
            )
            print(f"Submitting move: ({row}, {col}), strong={use_strong}")
            api.make_move(ROOM_ID, row, col, use_strong)
            runtime_state["last_move_key"] = move_key
        except ArenaAPIError as exc:
            print(f"[Move Error] {exc}")

    bid_request = room.get("bid_request") or {}
    if room.get("awaiting_bid") and bid_request:
        if bid_request.get("viewer_player_id") == my_player_id and not bid_request.get("viewer_submitted"):
            bid_key = f"{room.get('current_game_id')}:{bid_request.get('bid_deadline')}:{my_player_id}"
            if runtime_state.get("last_bid_key") == bid_key:
                return True
            my_bid_info = bid_request.get("players", {}).get(str(my_player_id), {})
            max_bid = float(my_bid_info.get("max_bid", 120.0) or 120.0)
            default_color = my_bid_info.get("default_color", "black")
            try:
                print("Armageddon bidding phase.")
                bid_seconds, chosen_color = strategy.choose_bid(max_bid, default_color)
                print(f"Submitting bid: {bid_seconds}s, color={chosen_color}")
                api.submit_bid(ROOM_ID, bid_seconds, chosen_color)
                runtime_state["last_bid_key"] = bid_key
            except ArenaAPIError as exc:
                print(f"[Bid Error] {exc}")

    return True


def stream_loop(presence_id, username, strategy, stop_event):
    runtime_state = {
        "last_ready_key": None,
        "last_move_key": None,
        "last_bid_key": None,
    }
    while not stop_event.is_set():
        try:
            print("Connecting to room stream...")
            response = api.get_stream_response(ROOM_ID, presence_id)
            client = sseclient.SSEClient(response)
            for event in client.events():
                if stop_event.is_set():
                    break
                if event.event == "sync":
                    continue
                try:
                    room = json.loads(event.data)
                except json.JSONDecodeError:
                    continue
                if not handle_room_snapshot(room, username, strategy, runtime_state):
                    stop_event.set()
                    break
        except Exception as exc:  # noqa: BLE001
            if stop_event.is_set():
                break
            print(f"[Stream Error] {exc}. Reconnecting in 1s...")
            time.sleep(1)


def main():
    print("Logging in...")
    login_data = api.login(API_KEY)
    bot_user = login_data["user"]
    username = bot_user["username"]
    print(f"Logged in as {bot_user['display_name']} (@{username})")

    strategy = BotStrategy(username)
    presence_id = uuid.uuid4().hex
    print(f"Using presence_id: {presence_id}")

    stop_event = threading.Event()
    heartbeat_thread = threading.Thread(target=heartbeat_loop, args=(presence_id, stop_event), daemon=True)
    heartbeat_thread.start()

    try:
        stream_loop(presence_id, username, strategy, stop_event)
    finally:
        stop_event.set()
        try:
            api.leave_room(ROOM_ID, presence_id)
        except Exception:  # noqa: BLE001
            pass


if __name__ == "__main__":
    main()
