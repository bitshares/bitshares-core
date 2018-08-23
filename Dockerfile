FROM phusion/baseimage:0.10.1
MAINTAINER The bitshares decentralized organisation

ENV LANG=en_US.UTF-8
RUN \
    apt-get update -y && \
    apt-get install -y \
      g++ \
      autoconf \
      cmake \
      git \
      libbz2-dev \
      libreadline-dev \
      libboost-all-dev \
      libcurl4-openssl-dev \
      libssl-dev \
      libncurses-dev \
      doxygen \
      ca-certificates \
    && \
    apt-get update -y && \
    apt-get install -y fish && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

ADD . /bitshares-core
WORKDIR /bitshares-core

# Compile
RUN \
    ( git submodule sync --recursive || \
      find `pwd`  -type f -name .git | \
	while read f; do \
	  rel="$(echo "${f#$PWD/}" | sed 's=[^/]*/=../=g')"; \
	  sed -i "s=: .*/.git/=: $rel/=" "$f"; \
	done && \
      git submodule sync --recursive ) && \
    git submodule update --init --recursive && \
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        . && \
    make witness_node cli_wallet && \
    install -s programs/witness_node/witness_node programs/cli_wallet/cli_wallet /usr/local/bin && \
    #
    # Obtain version
    mkdir /etc/bitshares && \
    git rev-parse --short HEAD > /etc/bitshares/version && \
    cd / && \
    rm -rf /bitshares-core

# Home directory $HOME
WORKDIR /
RUN useradd -s /bin/bash -m -d /var/lib/bitshares bitshares
ENV HOME /var/lib/bitshares
RUN chown bitshares:bitshares -R /var/lib/bitshares

# Volume
VOLUME ["/var/lib/bitshares", "/etc/bitshares"]

# rpc service:
EXPOSE 8090
# p2p service:
EXPOSE 1776

# default exec/config files
ADD docker/default_config.ini /etc/bitshares/config.ini
ADD docker/bitsharesentry.sh /usr/local/bin/bitsharesentry.sh
RUN chmod a+x /usr/local/bin/bitsharesentry.sh

# Make Docker send SIGINT instead of SIGTERM to the daemon
STOPSIGNAL SIGINT

# default execute entry
CMD ["/usr/local/bin/bitsharesentry.sh"]
