FROM ubuntu:22.04
RUN apt-get update && \
	apt-get install -y --no-install-recommends\
	build-essential \
	git \
	make \
	openmpi-common \
	openmpi-bin \
	libopenmpi-dev \
	libgsl-dev \
	pkg-config

WORKDIR /artis
COPY . .
RUN cp artisoptions_kilonova_lte.h artisoptions.h; \
	make
