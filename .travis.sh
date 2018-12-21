#!/usr/bin/env bash
cd ..
git clone --recursive https://github.com/polymonster/pmtech.git
ls
cd dr_scientist
python3 ../pmtech/tools/build.py -code -libs -ide gmake -platform osx
cd build/osx
make Makefile all
