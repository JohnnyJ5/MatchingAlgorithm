#!/usr/bin/env bash

CONTAINER_NAME="claude-workspace"
DOTFILES_REPO="git@github.com:JohnnyJ5/dotfiles.git"
DOTFILES_DIR=".claude_workspace_env/dotfiles"

DOCKER_COMMON=(
    -e HOME=/app/.claude_workspace_env
    -e GIT_SSH_COMMAND="ssh -i /app/.claude_workspace_env/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o IdentitiesOnly=yes"
    -v "$(pwd)":/app
    -v "$HOME/.ssh/claude_github:/app/.claude_workspace_env/.ssh/id_ed25519:ro"
)

dotfiles() {
    # Clone and install dotfiles on first run so ~/.claude agents/settings are available
    if [ ! -d "$DOTFILES_DIR" ]; then
        echo "Installing dotfiles..."
        docker run --rm "${DOCKER_COMMON[@]}" claude-cli-env bash -c "
            mkdir -p /app/.claude_workspace_env/.claude &&
            git clone ${DOTFILES_REPO} /app/.claude_workspace_env/dotfiles &&
            cd /app/.claude_workspace_env/dotfiles &&
            ./install.sh"
    else
        echo "Dotfiles already installed, updating..."
        docker run --rm "${DOCKER_COMMON[@]}" claude-cli-env bash -c "
            cd /app/.claude_workspace_env/dotfiles &&
            git pull origin main &&
            ./install.sh"
    fi
}


if [ "$(docker ps -q -f name=^${CONTAINER_NAME}$)" ]; then
    echo "Container '${CONTAINER_NAME}' is already running"
    
    dotfiles

    echo "Logging you into bash..."
    docker exec -it ${CONTAINER_NAME} bash
else
    echo "Starting a new '${CONTAINER_NAME}' container..."

    if [ ! -f "$HOME/.ssh/claude_github" ]; then
        echo "ERROR: SSH key not found at ~/.ssh/claude_github"
        exit 1
    fi

    mkdir -p .claude_workspace_env/.ssh

    if [ ! -f ".claude_workspace_env/.ssh/config" ]; then
        cat > .claude_workspace_env/.ssh/config <<'EOF'
Host github.com
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_ed25519
    IdentitiesOnly yes
    StrictHostKeyChecking no
EOF
        chmod 600 .claude_workspace_env/.ssh/config
    fi

    docker build -t claude-cli-env -f Dockerfile.claude .

    # Clone and install dotfiles on first run so ~/.claude agents/settings are available
    dotfiles

    docker run --rm -it --name ${CONTAINER_NAME} "${DOCKER_COMMON[@]}" claude-cli-env bash
fi
