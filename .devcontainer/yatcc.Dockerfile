# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS base

# 安装依赖
RUN <<EOF
apt-get update -y
apt-get install -y --no-install-recommends \
    ca-certificates tini build-essential git python3 \
    cmake ninja-build default-jdk bison flex lld
apt-get autoremove -y
apt-get clean -y
EOF

# 使用 tini 作为开发容器的 PID 1，和 docker run --init 是同样的效果
ENTRYPOINT ["/bin/tini", "--"]
CMD ["sleep", "infinity"]

# 完全版和基础版的区别在于预构建的文件，这会显著影响镜像大小
FROM base AS full

# 复制预构建的文件
COPY antlr/antlr.jar /dat/antlr/antlr.jar
COPY antlr/source /dat/antlr/source
COPY antlr/install /dat/antlr/install
COPY llvm/llvm /dat/llvm/llvm
COPY llvm/clang /dat/llvm/clang
COPY llvm/cmake /dat/llvm/cmake
COPY llvm/install /dat/llvm/install

# 设置环境变量
ENV YatCC_ANTLR_DIR=/dat/antlr \
    YatCC_LLVM_DIR=/dat/llvm
