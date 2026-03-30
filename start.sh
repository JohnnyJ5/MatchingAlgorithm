#!/usr/bin/env bash
set -euo pipefail
docker compose up -d --build
echo "Server running at http://localhost:8081"
