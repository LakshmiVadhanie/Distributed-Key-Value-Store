#!/usr/bin/env bash
export PATH="/opt/homebrew/bin:$PATH"

# Run backend
cd web-api
npm start &> ../api.log &
API_PID=$!
cd ..

# Run frontend on port 5173
cd web-ui
npm run dev -- --port 5173 &> ../ui.log &
UI_PID=$!

echo "API PID: $API_PID"
echo "UI PID: $UI_PID"
echo "Web Interface running at: http://localhost:5173"
