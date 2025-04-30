FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
      build-essential \
      cmake \
      ninja-build \
      git \
      libboost-dev \
      libboost-system-dev \
      catch2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

CMD ["bash"]
