FROM espressif/idf:v6.1

# Additional development tools
RUN apt-get update && apt-get install -y \
    clang-format \
    clang-tidy \
    usbutils \
    && rm -rf /var/lib/apt/lists/*

ENV IDF_PATH=/opt/esp/idf
ENV PATH="/opt/esp/python_env/idf6.1_py3.12_env/bin:${PATH}"

################################################################################
# AI support with OpenCode
################################################################################
# Switch to container user so AI tooling installs in the correct user paths
USER ubuntu

# Install OpenCode
RUN curl -fsSL https://opencode.ai/install | bash

# Create ~/.local/state so OpenCode has write permissions
RUN mkdir -p "$HOME/.local/state"