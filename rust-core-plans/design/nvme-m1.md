# NVMe M1 里程碑：结果记录

> 分支 `rust/nvme-lead`（本地，未推送）。所有数字为实测。

## 结论

**M1 达成**：Rust 驱动能在 QEMU 的 NVMe 控制器上完成 probe → 复位 → 建 admin 队列 →
Identify Controller → Identify Namespace → 打印型号/序列号/固件/容量 → 关闭控制器 → 卸载。
未注册块设备、未建 I/O 队列（M1 范围之外）。

## 实测输出（QEMU 8.2.0，`-device nvme`，64 MiB 后端）

```
nvme_rust_m1 0000:00:04.0: nvme-rust: CAP=0x4018200f0107ff
nvme_rust_m1 0000:00:04.0: nvme-rust: model='QEMU NVMe Ctrl' serial='deadbeef' fw='8.2.0'
nvme_rust_m1 0000:00:04.0: nvme-rust: capacity 67108864 bytes (131072 LBAs of 512 bytes)
```

容量与 QEMU 后端镜像大小（64 MiB）一致，说明 NSZE 与 LBA 格式解析正确。

## 实现要点

- 寄存器/命令布局按 NVMe 1.4 与 `drivers/nvme/host/pci.c` 的复位顺序实现；不复用 nvme-core。
- admin 队列用 `Coherent` DMA 分配，提交后轮询 CQ 相位位（无中断）。
- 与 `NVME_CORE` 互斥（同一组 PCI ID），M1 用 `CONFIG_NVME_RUST_M1` 单独开启。

## 构建与测试证据

| 项 | 值 |
| --- | --- |
| 内核构建 | `o-nvme-m1`，defconfig + `CONFIG_RUST=y` + `CONFIG_NVME_RUST_M1=m` + `CONFIG_WERROR=n` |
| 模块 | `nvme_rust_m1.ko` 29,488 B |
| QEMU 测试 | `tools/testing/rust/nvme_m1_qemu.sh`，PASS |
| 源码 sha256 | `cbbc3e33…` (driver) / `28e4833c…` (test script) |

## 与 04-nvme.md 规划的偏差

1. **规划假设“过渡期需要 pdu 参数化才能起步”不成立**。M1 不需要 per-request 私有数据：
   控制器 bring-up 只用到 admin 队列的管理命令，不涉及块层 request。pdu 参数化是 M2
   （I/O 队列）的前置，不是 M1 的。按 1.1 倍投入产出比的说法，这一步可以推迟。
2. **规划把 nvme 描述为“改一处全局受益”**，那只针对 pdu 参数化本身；M1 实测表明
   Rust 侧缺的底座（PCI 映射 `iomap_region_sized`、`Coherent` DMA、`io::Bar` 读写）
   已经够用，不需要先补新抽象。
3. **`tnvmcap` 在 QEMU 上恒为 0**，容量必须走 Identify Namespace 的 NSZE × LBA 大小。
   规划里没提这个坑。

## 遗留

- 未过 L1/L2 审核（本会话已达子代理嵌套上限，无法派审核代理）。
- 未推送；分支仅本地存在。
- M2 需要：I/O 队列、中断（MSI-X）、块设备注册，以及 pdu 参数化。