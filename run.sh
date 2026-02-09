#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="${1:-debug}"
PORT="${2:-35555}"
RECEIVER="$BUILD_DIR/udp_receiver_test"
PUBLISHER="$BUILD_DIR/xbox_udp_publisher"

if [[ ! -x "$RECEIVER" || ! -x "$PUBLISHER" ]]; then
    echo "Binaries not found. Run: ./build.sh $BUILD_DIR"
    exit 1
fi

# Keep shell open after command; run from project dir
RUN_RECEIVER="cd $SCRIPT_DIR && $RECEIVER $PORT; echo; echo 'Press Enter to close.'; read"
RUN_PUBLISHER="cd $SCRIPT_DIR && sleep 1 && $PUBLISHER 127.0.0.1 $PORT; echo; echo 'Press Enter to close.'; read"

spawn_two_terminals() {
    if command -v gnome-terminal &>/dev/null; then
        gnome-terminal --title="Xbox Receiver :$PORT" -- bash -c "$RUN_RECEIVER"
        sleep 0.5
        gnome-terminal --title="Xbox Publisher" -- bash -c "$RUN_PUBLISHER"
        return
    fi
    if command -v konsole &>/dev/null; then
        konsole -e bash -c "$RUN_RECEIVER" &
        sleep 0.5
        konsole -e bash -c "$RUN_PUBLISHER" &
        return
    fi
    if command -v xfce4-terminal &>/dev/null; then
        xfce4-terminal --title="Xbox Receiver :$PORT" -e "bash -c \"$RUN_RECEIVER\""
        sleep 0.5
        xfce4-terminal --title="Xbox Publisher" -e "bash -c \"$RUN_PUBLISHER\""
        return
    fi
    if command -v xterm &>/dev/null; then
        xterm -title "Xbox Receiver :$PORT" -e bash -c "$RUN_RECEIVER" &
        sleep 0.5
        xterm -title "Xbox Publisher" -e bash -c "$RUN_PUBLISHER" &
        return
    fi
    if command -v x-terminal-emulator &>/dev/null; then
        x-terminal-emulator -e bash -c "$RUN_RECEIVER" &
        sleep 0.5
        x-terminal-emulator -e bash -c "$RUN_PUBLISHER" &
        return
    fi
    return 1
}

if spawn_two_terminals; then
    echo "Started receiver and publisher in two terminal windows (port $PORT)."
else
    echo "No supported terminal found (tried gnome-terminal, konsole, xfce4-terminal, xterm)."
    echo "Run manually in two terminals:"
    echo "  Terminal 1: $RECEIVER $PORT"
    echo "  Terminal 2: $PUBLISHER 127.0.0.1 $PORT"
    exit 1
fi
