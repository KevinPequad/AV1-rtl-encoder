FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    verilator \
    python3 \
    python3-pip \
    ffmpeg \
    wget \
    git \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install numpy

WORKDIR /workspace

CMD ["bash"]
