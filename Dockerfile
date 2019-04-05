FROM ubuntu:18.04

RUN apt-get -y update
RUN apt-get -y install build-essential git

COPY . /tmp/build

WORKDIR /tmp/build

RUN make all

RUN cp /tmp/build/build-linux-std/bin/station /usr/local/bin

RUN rm -r /tmp/build

ENTRYPOINT [ "station", "-h $CONFIG_DIR" ]
