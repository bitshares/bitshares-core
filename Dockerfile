# This will build the witness_node in a docker image. Make sure you've already
# checked out the submodules before building.

FROM l3iggs/archlinux:latest
MAINTAINER Nathan Hourt <nathan@followmyvote.com>

RUN pacman -Syu --noconfirm gcc make autoconf automake cmake ninja boost libtool git

ADD . /bitshares-2
WORKDIR /bitshares-2
RUN cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .
RUN ninja witness_node || ninja -j 1 witness_node

RUN mkdir /data_dir
ADD docker/default_config.ini /default_config.ini
ADD docker/launch /launch
RUN chmod a+x /launch
VOLUME /data_dir

EXPOSE 8090 9090

ENTRYPOINT ["/launch"]
