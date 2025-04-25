# Dockerfile
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# 1) Install build tools, CMake/Ninja, git, and Boost (headers + system lib)
RUN apt-get update && apt-get install -y \
      build-essential \
      cmake \
      ninja-build \
      git \
      libboost-dev \
      libboost-system-dev \
    && rm -rf /var/lib/apt/lists/*

# 2) Set your working directory (you’ll mount your code here)
WORKDIR /app

# 3) By default drop into bash
CMD ["bash"]
