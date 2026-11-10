# NVMe 最小 Rust 骨架（M1）

> 分支 `rust/nvme-skel`，worktree `.wt/nvme-skel`。

## 范围与结果

在 QEMU 的 NVMe 控制器上完成：probe → 控制器复位 → admin 队列 → Identify Controller →
Identify Namespace → 打印 → 关闭控制器 → 卸载。**不做 I/O 队列，不做块设备注册。**

实测串口输出（QEMU 8.2.0，`-device nvme`，64 MiB 后端）：

```
nvme_skel 0000:00:04.0: nvme-rust: CAP=0x4018200f0107ff
nvme_skel 0000:00:04.0: nvme-rust: model='QEMU NVMe Ctrl' serial='deadbeef' fw='8.2.0'
nvme_skel 0000:00:04.0: nvme-rust: capacity 67108864 bytes (131072 LBAs of 512 bytes)
```

## 设计决策

1. **用 bindings 而不是手抄结构**：`bindings::nvme_id_ctrl`、`bindings::nvme_id_ns`、
   `bindings::nvme_command`、`bindings::nvme_completion` 全部来自 `#include <linux/nvme.h>`。
   字段名（`mn`/`sn`/`fr`/`nsze`/`flbas`/`lbaf[].ds`）与 C 完全一致，C 侧改字段名 Rust 直接编译失败。
2. **按 PCI class 匹配，不按 VID:DID**：`pci::DeviceId::from_class(0x010802, 0xffffff)`，
   与 C 的 `nvme_pci_tbl` 用 class 匹配保持一致，不绑定到 QEMU 的 Red Hat ID。
3. **admin 队列用 coherent DMA**（`Coherent`），提交后轮询 CQ 相位位，不装中断——M1 只跑同步管理命令，
   中断留给 M2（I/O 队列）。
4. **与 `NVME_CORE` 互斥**：同一组 PCI class，两个驱动不能同时认领。

## C 参考点

| 项 | C 位置 |
| --- | --- |
| 寄存器布局与复位顺序 | `drivers/nvme/host/pci.c`（nvme_pci_enable / nvme_pci_disable） |
| admin 队列深度与 AQA 编码 | 同上 + NVMe 1.4 §3.1 |
| 命令与完成结构 | `include/linux/nvme.h:1992`（nvme_command）、`:2252`（nvme_completion） |
| Identify 结构 | `include/linux/nvme.h:309`（nvme_id_ctrl）、nvme_id_ns |
| class 匹配表 | `drivers/nvme/host/pci.c` 的 nvme_pci_tbl（class 0x010802） |

## 构建与加载证据

| 项 | 值 |
| --- | --- |
| 构建 | 编译机 `o-nvme-m1`，`defconfig + CONFIG_RUST=y + CONFIG_NVME_SKEL=m + CONFIG_WERROR=n` |
| 模块 | `nvme_skel.ko` 28,320 B，modpost 通过 |
| 加载测试 | `tools/testing/rust/nvme_m1_qemu.sh` → PASS |
| bindings 变化 | `rust/bindings/bindings_helper.h` +`#include <linux/nvme.h>`，产物 6,420,696 → 待测字节数（见下） |

## 未实现 / 简化

- 无 I/O 队列、无中断（MSI-X）、无块设备注册——M2 范围。
- 无超时中断处理：轮询用固定次数上限（`POLL_LIMIT`），超时返回 `ETIMEDOUT`。
- 未处理 `CC.SHUTDOWN` 的优雅关机，直接 `CC.EN=0`。
- 未做多 namespace 遍历，只查 NSID 1。
- 未处理 admin 队列深度超过 4 的情况（固定 4 项，够 M1 用）。