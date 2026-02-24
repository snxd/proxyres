#!/bin/sh
# Start an HTTP server and wait until it is ready to accept connections.

PORT="${1:-8080}"
MAX_ATTEMPTS=30

python3 -m http.server "$PORT" &
SERVER_PID=$!

for i in $(seq 1 "$MAX_ATTEMPTS"); do
    if curl -sSf "http://127.0.0.1:$PORT/" >/dev/null 2>&1; then
        echo "HTTP server is ready on port $PORT (pid $SERVER_PID)"
        exit 0
    fi
    echo "Waiting for HTTP server (attempt $i/$MAX_ATTEMPTS)..."
    sleep 1
done

echo "HTTP server failed to start on port $PORT" >&2
kill "$SERVER_PID" 2>/dev/null
exit 1
