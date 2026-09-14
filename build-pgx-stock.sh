#!/bin/sh
# 用 pgx 机器的全量配置构建本树（出树、单一构建）。
#
# 用法:
#   sh build-pgx-stock.sh <objdir> [make 目标...]        # 默认 Image modules
#   sh build-pgx-stock.sh /data5/home2-real/guanwentao/out-hepburn-stock bindeb-pkg
#
# 说明: O= 指向仓库外，deb 会落在 objdir 的上一级，不污染 git 树；
#       配置 = arch/arm64/configs/pgx_stock_full.config 经 olddefconfig
#             + pgx_stock_build_exceptions.txt 里列出的缺源模块关闭。
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
OBJARG=${1:?usage: build-pgx-stock.sh <objdir> [make targets...]}
shift
TARGETS=${*:-"Image modules"}
mkdir -p "$OBJARG"
OBJ=$(cd "$OBJARG" && pwd)
cp "$SRC/arch/arm64/configs/pgx_stock_full.config" "$OBJ/.config"
MK="make -C $SRC O=$OBJ ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LLVM= LLVM_IAS="
$MK olddefconfig
while read -r s; do
    case "$s" in ""|\#*) continue ;; esac
    "$SRC/scripts/config" --file "$OBJ/.config" -d "$s"
done < "$SRC/arch/arm64/configs/pgx_stock_build_exceptions.txt"
$MK olddefconfig
exec $MK KCFLAGS="-Wno-error=strict-prototypes" -j"$(nproc)" $TARGETS
