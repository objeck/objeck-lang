#!/bin/bash
# Objeck Playground - Zero-downtime update script
# Usage: sudo -u playground bash update.sh

set -euo pipefail

PLAYGROUND_DIR="/opt/playground"

echo "=== Updating Objeck Playground ==="

cd "$PLAYGROUND_DIR"

# Pull latest code
echo ""
echo "--- Pulling latest code ---"
git pull origin master

# Update Python dependencies if changed
echo ""
echo "--- Updating dependencies ---"
/opt/playground/venv/bin/pip install -q -r backend/requirements.txt

# Rebuild sandbox image if Dockerfile changed
echo ""
echo "--- Rebuilding sandbox image ---"
bash docker/build-sandbox.sh

# Restart the API service
echo ""
echo "--- Restarting API ---"
sudo systemctl restart playground

# Clean up old Docker images
echo ""
echo "--- Cleaning up ---"
docker image prune -f
docker container prune -f

# Health check
echo ""
echo "--- Health check ---"
sleep 2
if curl -sf http://localhost:8000/api/health > /dev/null; then
    echo "API is healthy"
else
    echo "WARNING: API health check failed!"
    sudo systemctl status playground --no-pager
fi

echo ""
echo "=== Update complete ==="
