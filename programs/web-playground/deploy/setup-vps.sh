#!/bin/bash
# Objeck Playground - VPS provisioning script (all-on-VPS)
# Target: Ubuntu 22.04+ (DigitalOcean, Linode, Hetzner, etc.)
# Usage: sudo bash setup-vps.sh

set -euo pipefail

DOMAIN="${1:-playground.objeck.org}"

echo "=== Objeck Playground VPS Setup ==="
echo "Domain: $DOMAIN"

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
apt-get install -y python3 python3-pip python3-venv git

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
chown -R playground:playground /opt/playground /tmp/playground

# Summary
VPS_IP=$(curl -s ifconfig.me 2>/dev/null || echo "<unknown>")
echo ""
echo "=== Setup Complete ==="
echo ""
echo "VPS IP: ${VPS_IP}"
echo ""
echo "Next steps:"
echo ""
echo "  1. Clone the repo:"
echo "     sudo -u playground git clone https://github.com/objeck/objeck-lang.git /opt/playground/repo"
echo ""
echo "  2. Link the playground code:"
echo "     ln -sf /opt/playground/repo/programs/web-playground/backend /opt/playground/backend"
echo "     ln -sf /opt/playground/repo/programs/web-playground/frontend /opt/playground/frontend"
echo "     ln -sf /opt/playground/repo/programs/web-playground/demos /opt/playground/demos"
echo "     ln -sf /opt/playground/repo/programs/web-playground/docker /opt/playground/docker"
echo ""
echo "  3. Set up Python venv:"
echo "     python3 -m venv /opt/playground/venv"
echo "     /opt/playground/venv/bin/pip install -r /opt/playground/backend/requirements.txt"
echo ""
echo "  4. Build sandbox Docker image:"
echo "     sudo -u playground bash /opt/playground/docker/build-sandbox.sh /opt/playground/repo/core/release/deploy"
echo ""
echo "  5. Install Caddyfile:"
echo "     cp /opt/playground/repo/programs/web-playground/deploy/Caddyfile /etc/caddy/Caddyfile"
echo "     systemctl restart caddy"
echo ""
echo "  6. Install systemd service:"
echo "     cp /opt/playground/repo/programs/web-playground/deploy/playground.service /etc/systemd/system/"
echo "     systemctl daemon-reload"
echo "     systemctl enable --now playground"
echo ""
echo "  7. Set DNS A record:"
echo "     ${DOMAIN} -> ${VPS_IP}"
echo ""
echo "  8. Verify:"
echo "     curl https://${DOMAIN}/api/health"
