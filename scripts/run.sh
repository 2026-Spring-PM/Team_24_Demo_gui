#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-hyorilee33/team_24_project:0.1.0}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"


docker pull --platform linux/amd64 "$IMAGE_NAME"

case "$(uname -s)" in
    Darwin)
        docker run -it --rm \
            --platform linux/amd64 \
            -v "$ROOT_DIR":/workspace \
            -e DISPLAY=host.docker.internal:0 \
            -e SDL_VIDEODRIVER=x11 \
            -e SDL_RENDER_DRIVER=software \
            -e XDG_RUNTIME_DIR=/tmp \
            "$IMAGE_NAME" \
            /workspace/main "$@"
        ;;
    Linux)
        docker run -it --rm \
            --platform linux/amd64 \
            -v "$ROOT_DIR":/workspace \
            -v /tmp/.X11-unix:/tmp/.X11-unix \
            -e DISPLAY="${DISPLAY:-:0}" \
            -e SDL_RENDER_DRIVER=software \
            -e XDG_RUNTIME_DIR=/tmp \
            "$IMAGE_NAME" \
            /workspace/main "$@"
        ;;
    *)
        echo "Unsupported OS. Try: bash scripts/smoke.sh"
        exit 1
        ;;
esac
