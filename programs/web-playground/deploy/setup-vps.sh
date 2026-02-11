#!/bin/bash
# Objeck Playground - VPS provisioning script
# Target: Ubuntu 22.04+ (DigitalOcean, AWS Lightsail, etc.)
# Usage: sudo bash setup-vps.sh

set -euo pipefail

echo "=== Objeck Playground VPS Setup ==="

# System update
echo ""
echo "--- System Update ---"
apt-get update && apt-get upgrade -y

# Install Docker
echo ""
echo "--- Installing Docker ---"
if ! command -v docker &> /dev/null; then
    curl -fsSL https://get.docker.com | sh
    systemctl enable docker
    systemctl start docker
else
    echo "Docker already installed"
fi

# Install Caddy
echo ""
echo "--- Installing Caddy ---"
if ! command -v caddy &> /dev/null; then
    apt-get install -y debian-keyring debian-archive-keyring apt-transport-https
    curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | \
        gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
    curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | \
        tee /etc/apt/sources.list.d/caddy-stable.list
    apt-get update && apt-get install -y caddy
else
    echo "Caddy already installed"
fi

# Install Python
echo ""
echo "--- Installing Python ---"
apt-get install -y python3 python3-pip python3-venv

# Configure firewall
echo ""
echo "--- Configuring Firewall ---"
ufw allow 22/tcp
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable

# Create application user
echo ""
echo "--- Creating Application User ---"
if ! id playground &> /dev/null; then
    useradd -r -m -s /bin/bash playground
    usermod -aG docker playground
else
    echo "User 'playground' already exists"
fi

# Create directories
echo ""
echo "--- Creating Directories ---"
mkdir -p /opt/playground/data
mkdir -p /tmp/playground
mkdir -p /var/log/caddy
chown playground:playground /opt/playground /opt/playground/data /tmp/playground

# Summary
VPS_IP=$(curl -s ifconfig.me 2>/dev/null || echo "<unknown>")
echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "  1. Copy application code to /opt/playground"
echo "  2. Set up Python venv:"
echo "     cd /opt/playground/backend"
echo "     python3 -m venv /opt/playground/venv"
echo "     /opt/playground/venv/bin/pip install -r requirements.txt"
echo "  3. Build sandbox Docker image:"
echo "     cd /opt/playground && bash docker/build-sandbox.sh"
echo "  4. Copy Caddyfile to /etc/caddy/Caddyfile and restart caddy"
echo "  5. Install systemd service:"
echo "     cp deploy/playground.service /etc/systemd/system/"
echo "     systemctl daemon-reload"
echo "     systemctl enable --now playground"
echo "  6. Set DNS A record:"
echo "     api.objeck.org -> ${VPS_IP}"
echo "     playground.objeck.org -> ${VPS_IP} (optional)"
