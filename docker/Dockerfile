FROM l3iggs/archlinux:latest
MAINTAINER Nathan Hourt <nathan@followmyvote.com>

RUN pacman -Syu --noconfirm gcc make autoconf automake cmake ninja boost libtool git

RUN git clone https://github.com/bitshares/bitshares-2
WORKDIR /bitshares-2
RUN git submodule update --init --recursive
RUN cmake -G Ninja .
RUN ninja witness_node

RUN mkdir /data_dir
ADD default_config.ini /default_config.ini
ADD launch /launch
RUN chmod a+x /launch
VOLUME /data_dir

EXPOSE 8091 9090

ENTRYPOINT ["/launch"]
