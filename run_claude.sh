#!/usr/bin/env bash

CONTAINER_NAME="claude-workspace"

if [ "$(docker ps -q -f name=^${CONTAINER_NAME}$)" ]; then
    echo "Container '${CONTAINER_NAME}' is already running. Logging you into bash..."
    docker exec -it ${CONTAINER_NAME} bash
else
    echo "Starting a new '${CONTAINER_NAME}' container..."

    if [ ! -f "$HOME/.ssh/claude_github" ]; then
        echo "ERROR: SSH key not found at ~/.ssh/claude_github"
        exit 1
    fi

    mkdir -p .claude_config/.ssh

    docker build -t claude-cli-env -f Dockerfile.claude .

    docker run --rm -it --name ${CONTAINER_NAME} \
        -e HOME=/app/.claude_config \
        -e GIT_SSH_COMMAND="ssh -i /app/.claude_config/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o IdentitiesOnly=yes" \
        -v "$(pwd)":/app \
        -v "$HOME/.ssh/claude_github:/app/.claude_config/.ssh/id_ed25519:ro" \
        claude-cli-env
fi
