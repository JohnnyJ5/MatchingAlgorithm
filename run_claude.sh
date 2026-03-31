#!/usr/bin/env bash

CONTAINER_NAME="claude-workspace"
DOTFILES_REPO="git@github.com:JohnnyJ5/dotfiles.git"
DOTFILES_DIR=".claude_config/dotfiles"

DOCKER_COMMON=(
    -e HOME=/app/.claude_config
    -e GIT_SSH_COMMAND="ssh -i /app/.claude_config/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o IdentitiesOnly=yes"
    -v "$(pwd)":/app
    -v "$HOME/.ssh/claude_github:/app/.claude_config/.ssh/id_ed25519:ro"
)

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

    # Clone and install dotfiles on first run so ~/.claude agents/settings are available
    if [ ! -d "$DOTFILES_DIR" ]; then
        echo "Installing dotfiles..."
        docker run --rm "${DOCKER_COMMON[@]}" claude-cli-env bash -c "
            mkdir -p /app/.claude_config/.claude &&
            git clone ${DOTFILES_REPO} /app/.claude_config/dotfiles &&
            cd /app/.claude_config/dotfiles &&
            ./install.sh
        "
    fi

    docker run --rm -it --name ${CONTAINER_NAME} "${DOCKER_COMMON[@]}" claude-cli-env
fi
