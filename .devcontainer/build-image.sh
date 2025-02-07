#!/bin/bash
set -e
cd $(dirname "$0")

../antlr/setup.sh
../llvm/setup.sh

if [ -z "$YatCC_ANTLR_DIR" ]; then
  YatCC_ANTLR_DIR=$(realpath ../antlr)
fi
if [ -z "$YatCC_LLVM_DIR" ]; then
  YatCC_LLVM_DIR=$(realpath ../llvm)
fi

rm -rf antlr llvm
mkdir antlr llvm
cp -r --link $YatCC_ANTLR_DIR/antlr.jar antlr/antlr.jar
cp -r --link $YatCC_ANTLR_DIR/source antlr/source
cp -r --link $YatCC_ANTLR_DIR/install antlr/install
cp -r --link $YatCC_LLVM_DIR/llvm llvm/llvm
cp -r --link $YatCC_LLVM_DIR/clang llvm/clang
cp -r --link $YatCC_LLVM_DIR/cmake llvm/cmake
cp -r --link $YatCC_LLVM_DIR/install llvm/install
docker build -f yatcc.Dockerfile --target base -t yatcc:base .
docker tag yatcc:base yatcc:base.$(date +%Y-%m-%d.%H-%M-%S.%N)
docker build -f yatcc.Dockerfile --target full -t yatcc:full .
docker tag yatcc:full yatcc:full.$(date +%Y-%m-%d.%H-%M-%S.%N)
docker tag yatcc:full yatcc:latest
