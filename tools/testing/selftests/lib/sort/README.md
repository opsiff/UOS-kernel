# lib/sort 的 C/Rust 差分基准

用来复现 `rust: add lib algorithm helpers backed by the Rust sort implementation`
提交信息里的比较次数与 swap 次数。

**过渡工具**：随 `lib/sort.c` 的 C 半边一起删除。

## 文件

- `sort_c_orig.c` —— 基线 `lib/sort.c` 的 C 原实现，只改名不改逻辑
- `sort_rs.rs` —— **由 `mk-sort-rs.sh` 生成**，内容 = `rust/kernel/algo/sort.rs`
  （算法）+ `rust/kernel/sort.rs`（C ABI 门面）。不要手改；树内源码变了就重跑生成脚本
- `main.c` —— 差分 + 计时驱动：同一份输入分别喂给 C 版和 Rust 版，逐字节比对输出，
  并统计比较次数
- `swap_test.c` —— 自定义 `swap_func` + `priv` 路径的差分测试；它自带
  `__cond_resched()`，所以链接时**不要**再带 `stub.o`
- `stub.c` —— `__cond_resched()` 的用户态桩，只给 `bench` 那条链接用
- `inc/` —— 让 `lib/sort.c` 能在用户态编译的最小 `linux/*.h` 桩

## 编译与运行

```sh
make		# 生成 sort_rs.rs，并编译 bench 与 swap_test
./bench
./swap_test
```

工具链可以用变量覆盖（默认分别是 `clang-21` 和 `rustc`）：

```sh
make CLANG=clang-18 RUSTC=rustc
```

等价的手工命令（`make` 只是把它固定下来）：

```sh
./mk-sort-rs.sh
${CLANG:-clang-21} -O2 -I inc -c sort_c_orig.c \
    -Dsort_r=sort_r_c -Dsort=sort_c \
    -Dsort_nonatomic=sort_nonatomic_c -Dsort_r_nonatomic=sort_r_nonatomic_c \
    -o sort_c.o
${CLANG:-clang-21} -O2 -I inc -c stub.c -o stub.o
${RUSTC:-rustc} --edition 2021 -O --crate-type staticlib \
    --cfg CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS sort_rs.rs -o libsortrs.a
${CLANG:-clang-21} -O2 -I inc main.c sort_c.o stub.o libsortrs.a -lpthread -ldl -lm -o bench
${CLANG:-clang-21} -O2 -I inc swap_test.c sort_c.o libsortrs.a -lpthread -ldl -lm -o swap_test
```

`sort_rs.rs` 是生成物，不要提交、不要手工编辑：它必须跟着
`rust/kernel/algo/sort.rs` 与 `rust/kernel/sort.rs` 走，否则基准测的就不是树里的代码了。

## 工具链

**比较次数与 swap 次数与编译器版本无关**：clang 21.1.8 与 clang-18.1.8（配 rustc 1.95）
跑出的计数逐位相同，因为两版实现都是逐条对应的字节搬移与比较，没有可被优化的差异。

**只有毫秒数随机器/负载/编译器漂移**（同一台机器上并发构建时能差 30%）。README 表格里的
耗时是 2×Xeon 8352S + clang 21.1.8 的参考值；换机器只应比对同一轮内的 C/Rust 相对值。

## 输入

`main.c` 内部生成，不依赖外部数据：`srand(12345+pattern)`，每个元素是
`((uint64_t)rand() << 32) ^ rand()`；三种模式：

- `random`  —— 纯随机
- `sorted`  —— `v[i] = i`
- `nearly`  —— 排序后随机交换 `n/100` 对

规模 1k / 100k / 1M，元素大小 8 字节。

## 预期输出（3 轮中位数，2×Xeon 8352S）

| pattern | n | C (ms) | Rust (ms) | 比较次数 |
|---|---|---|---|---|
| random | 1M | 679.0 | 690.7 | 20,294,631（两版相同）|
| sorted | 1M | 246.8 | 248.9 | 20,458,492（两版相同）|
| nearly | 1M | 277.3 | 279.7 | 20,448,686（两版相同）|
| random | 100k | 28.3 | 26.4 | 1,699,018（两版相同）|

末尾打印 `all outputs identical` 表示两版输出逐字节一致；`swap_test` 打印
`swaps C=258346 Rust=258346 -> identical`。
