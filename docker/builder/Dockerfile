FROM ubuntu:xenial
MAINTAINER The bitshares decentralized organisation

RUN \
apt-get update -y && \
apt-get install -y \
  g++ \
  autoconf \
  cmake \
  git \
  libbz2-dev \
  libreadline6-dev \
  libboost-all-dev \
  libssl-dev \
  libncurses-dev \
  doxygen

RUN \
  apt-get update -y && \
  apt-get install -y fish

RUN useradd bitshares-builder -md /bitshares
