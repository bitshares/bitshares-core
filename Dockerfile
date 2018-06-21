FROM phusion/baseimage:0.10.1
MAINTAINER The ULF decentralized financial services

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

ADD . /ULF-CLI
WORKDIR /ULF-CLI

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
    make install && \
    #
    # Obtain version
    mkdir /etc/ULF && \
    git rev-parse --short HEAD > /etc/ULF/version && \
    cd / && \
    rm -rf /ULF-CLI

# Home directory $HOME
WORKDIR /
RUN useradd -s /bin/bash -m -d /var/lib/ULF ULF
ENV HOME /var/lib/ULF
RUN chown ULF:ULF -R /var/lib/ULF

# Volume
VOLUME ["/var/lib/ULF", "/etc/ULF"]

# rpc service:
EXPOSE 8090
# p2p service:
EXPOSE 2001

# default exec/config files
ADD docker/default_config.ini /etc/ULF/config.ini
ADD docker/ULFentry.sh /usr/local/bin/ULFentry.sh
RUN chmod a+x /usr/local/bin/ULFentry.sh

# default execute entry
CMD /usr/local/bin/ULFentry.sh
