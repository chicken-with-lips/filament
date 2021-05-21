#Dockerfile

FROM ubuntu:18.04

RUN apt-get update \
  && apt-get install -y apt-transport-https ca-certificates gnupg software-properties-common wget \
  && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | apt-key add - \
  && apt-add-repository 'deb https://apt.kitware.com/ubuntu/ bionic main' \
  && apt-get update

RUN apt-get install -y cmake ninja-build python3-distutils
RUN apt-get install -y clang-7 libglu1-mesa-dev libc++-7-dev libc++abi-7-dev
RUN apt-get install -y libxi-dev
RUN apt-get install -y emacs-nox git curl wget unzip
RUN apt-get install -y libsdl2-dev
RUN apt-get install -y libglfw3-dev
RUN apt-get install -y swig

RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-7 100
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-7 100
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100
RUN update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100

WORKDIR /tmp
