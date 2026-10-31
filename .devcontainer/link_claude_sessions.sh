#!/bin/bash

# Claude Code stores sessions per working directory under ~/.claude/projects/<path with non-alphanumerics as '-'>.
# The workspace path differs between host and container, so link the container's key to the host's so both
# see the same sessions. ~/.claude/projects is a per-container volume and the host's projects folder is mounted
# at ~/.claude-host-projects, so the link only exists in this container and can't collide with other containers
# that use the same workspace path.

set -e  # Exit on any error

PROJECTS_DIR="${HOME}/.claude/projects"
HOST_PROJECTS_DIR="${HOME}/.claude-host-projects"
HOST_KEY=$(echo "${HOST_WORKSPACE_FOLDER}" | sed 's/[^a-zA-Z0-9]/-/g')
CONTAINER_KEY=$(echo "${CONTAINER_WORKSPACE_FOLDER}" | sed 's/[^a-zA-Z0-9]/-/g')

if [ -z "$HOST_KEY" ] || [ -z "$CONTAINER_KEY" ]; then
    echo "Nothing to link for Claude Code sessions."
    exit 0
fi

mkdir -p "${HOST_PROJECTS_DIR}/${HOST_KEY}"

if [ -e "${PROJECTS_DIR}/${CONTAINER_KEY}" ] && [ ! -L "${PROJECTS_DIR}/${CONTAINER_KEY}" ]; then
    echo "${PROJECTS_DIR}/${CONTAINER_KEY} already exists as a directory. Not linking Claude Code sessions."
    exit 0
fi

ln -sfn -- "${HOST_PROJECTS_DIR}/${HOST_KEY}" "${PROJECTS_DIR}/${CONTAINER_KEY}"
echo "Linked Claude Code sessions: ${CONTAINER_KEY} -> host ${HOST_KEY}"
