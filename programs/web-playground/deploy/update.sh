#!/bin/bash
# Objeck Playground - Zero-downtime update script
# Usage: sudo bash update.sh

set -euo pipefail

REPO_DIR="/opt/playground/repo"

echo "=== Updating Objeck Playground ==="

# Pull latest code
echo ""
echo "--- Pulling latest code ---"
cd "$REPO_DIR"
sudo -u playground git pull origin master

# Update Python dependencies if changed
echo ""
echo "--- Updating dependencies ---"
/opt/playground/venv/bin/pip install -q -r /opt/playground/backend/requirements.txt

# Rebuild sandbox image if Dockerfile changed
echo ""
echo "--- Rebuilding sandbox image ---"
sudo -u playground bash /opt/playground/docker/build-sandbox.sh "$REPO_DIR/core/release/deploy"

# Restart the API service
echo ""
echo "--- Restarting API ---"
systemctl restart playground

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
    systemctl status playground --no-pager
fi

echo ""
echo "=== Update complete ==="
