#!/usr/bin/env bash
set -e # exit immediately if any command fails
echo "STICH DEBUG BUILD" 
cc -g -DDEBUG -DSTICH_SLOW stich.c -o stich