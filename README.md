# :brain: CudaBrainSecp（中文说明）
基于 CUDA 的 Secp256k1 脑钱包/助记词恢复工具。核心思路是在 GPU 上直接执行 Secp256k1 椭圆曲线点乘（Point Multiplication），配合 SHA‑256 与 RIPEMD‑160 的地址哈希与快速匹配，针对“先哈希再做椭圆曲线”的密钥生成流程提供高吞吐的搜索/校验能力。

## :notebook_with_decorative_cover: 架构概览
整体数据/控制流如下图（原作者示意）：
![DiagramV12](https://user-images.githubusercontent.com/8969128/185214693-8632ee9b-b748-4cd5-bf43-77434ea62284.png)

代码自顶向下分为三层：
- 应用编排（`CudaBrainSecp.cpp`）
  - 合并并精简哈希库：遍历 `TestHash/` 中的 Hash160 文件，仅保留每个 Hash160 的后 8 字节、去重写入二进制缓冲，显著降低匹配成本（见 `CPU/HashMerge.cpp`）。
  - 生成 GPU 用的 GTable（预计算的 G 点表）：调用 CPU 端 SECP256K1 实现预计算 16 个 16bit 分块、共 ~1048576 个点（见 `CPU/SECP256K1.cpp`），再拷贝到线性数组给 GPU 消费（`loadGTable`）。
  - 加载输入词表/哈希缓冲区，选择运行模式（Books/Combo），循环发起 GPU kernel 迭代并打印命中。
- GPU 计算（`GPU/GPUSecp.cu` + 头文件）
  - Kernel：`CudaRunSecp256k1Books`、`CudaRunSecp256k1Combo`
  - 设备函数：`_PointMultiSecp256k1`（核心点乘）、`_GetHash160*`（哈希）、`_BinarySearch`（命中检查）
  - 管理类：`class GPUSecp` 负责设备选择、内存分配/拷贝、迭代与结果回传
- CPU 椭圆曲线与大整数库（`CPU/Int.*`, `CPU/Point.*`, `CPU/SECP256K1.*`）
  - 用于生成 GTable（供 GPU 点乘查表+累加），以及部分工具逻辑

目录结构要点：
- `CudaBrainSecp.cpp`：程序入口与流程编排
- `CPU/`
  - `Int.h/.cpp`、`IntMod.cpp`：定长大整数与模运算
  - `Point.h/.cpp`：椭圆曲线点类型与辅助操作
  - `SECP256k1.h/.cpp`：SECP256K1 曲线、GTable 预计算、点加/倍点（CPU 端）
  - `HashMerge.cpp`：合并 `TestHash/` 下所有 Hash160 文件，提取末 8 字节进入去重有序集合，写出 `merged-sorted-unique-8-byte-hashes`
  - `Combo.cpp`：组合遍历辅助（为 Combo 模式跨迭代推进起始游标）
- `GPU/`
  - `GPUSecp.h`：配置项与常量（线程拓扑、词表长度、输入规模等）与 `class GPUSecp` 声明
  - `GPUSecp.cu`：Kernel 与主流程（Books/Combo 两种模式），点乘与命中记录
  - `GPUMath.h`：SECP256K1 底层大整数/模运算、点加（大量内联 PTX 优化）
  - `GPUHash.h`：SHA‑256、RIPEMD‑160、P2PKH/P2SH 的 Hash160 计算，及书本/组合输入的专用变体
- `TestBook/`：示例 Prime/Affix 词表
- `TestHash/`：示例 Hash160 文件集（运行时会被合并）
- `Makefile`：构建脚本（可配置目标架构）

## :books: 两种运行模式
- ModeBooks（默认）
  - 每线程取一个 Affix 词，与全部 Prime 词组合；拼接后做 SHA‑256 得到 32 字节私钥。
  - 使用预计算 GTable 在 GPU 上做点乘得到公钥，计算压缩/非压缩两种 Hash160。
  - 取 Hash160 的后 8 字节，在升序的 8 字节列表中做二分查找（`_BinarySearch`），命中则记录哈希与对应私钥。
  - 关键函数：`CudaRunSecp256k1Books`（kernel）、`_SHA256Books`、`_PointMultiSecp256k1`、`_GetHash160Comp`/`_GetHash160`、`_BinarySearch`。

- ModeCombo（可选）
  - 将输入空间看作“组合锁”，用 `COMBO_SYMBOLS` 所定义的字符集做全排列遍历。
  - 每次迭代由 CPU 用 `adjustComboBuffer` 推进起始游标，Kernel 内部每线程完成局部搜索。
  - 其它流程（点乘、哈希、匹配）与 Books 模式相同。
  - 关键函数：`CudaRunSecp256k1Combo`（kernel）、`_SHA256Combo`、`_FindComboStart`。

## :key: BIP39 助记词恢复（新增）
- 模式说明
  - CPU 端实现 BIP39：PBKDF2-HMAC-SHA512（2048 次）得到 seed[64]
  - CPU 端实现 BIP32：主/子私钥推导（支持硬化/非硬化）
  - GPU 端：对导出的 32 字节私钥批量做点乘 + Hash160（压缩/非压缩）+ 二分命中
- 入口与用法
  - 运行：`./CudaBrainSecp --bip39 --mnemonics=mnemonics.txt --path=m/44'/0'/0'/0/0 --range=0:100 --pass=YOUR_PASS`
    - 注意：shell 里带 `'` 的路径需要正确转义或用双引号包裹，例如：`--path="m/44'/0'/0'/0/0"`
  - 参数
    - `--mnemonics=FILE`：每行一个助记词（ASCII 或预先 NFKD 规范化）
    - `--pass=STR`：BIP39 passphrase（可空）
    - `--path=PATH`：BIP32 路径（默认 `m/44'/0'/0'/0/0`）
    - `--range=START:COUNT`：对末级索引做区间遍历（默认 `0:1`）
- 代码位置
  - CPU 实现：`CPU/BIP39.cpp`、`CPU/BIP39.h`
  - 新内核/通道：`CudaRunSecp256k1PrivList`、`GPUSecp::doIterationSecp256k1PrivList`
  - 构造器：`GPUSecp(countPriv, privListCPU, ...)` 直接接收私钥列表


## :triangular_ruler: 主要函数与核心逻辑
- 应用层（`CudaBrainSecp.cpp`）
  - `main`
    - 调整 CPU 栈（`increaseStackSizeCPU`）→ 合并哈希（`mergeHashes`）→ 生成 GTable（`loadGTable`）→ 加载哈希缓冲区（`loadInputHash`）→ 启动模式（默认 `startSecp256k1ModeBooks`）。
  - `mergeHashes(name_hash_folder, name_hash_buffer)`（见 `CPU/HashMerge.cpp`）
    - 遍历目录，将所有 Hash160 文件拼接成临时文件；读取全部 20 字节 Hash160，提取末 8 字节进有序 `set<uint64_t>` 去重；将唯一值顺序写出到 `merged-sorted-unique-8-byte-hashes`。
  - `loadGTable(gTableX, gTableY)`
    - 调用 `Secp256K1::Init()` 构建 GTable（按 16×16bit 分块预计算）；将每个点的 X/Y 32 字节坐标拷贝到线性数组，供 GPU 侧直接读取。
  - `startSecp256k1ModeBooks/Combo`
    - 创建 `GPUSecp`，把 GTable/词表/哈希缓冲拷贝到 GPU；循环调用 `doIterationSecp256k1Books/Combo` 执行 Kernel，迭代后用 `doPrintOutput` 打印/落盘。

- GPU 管理与 Kernel（`GPU/GPUSecp.cu`）
  - `class GPUSecp`
    - 构造：设置设备/限制（栈大小等）、申请/拷贝输入与输出（部分输出用 `cudaHostAlloc` 固定页内存）、打印设备信息。
    - `doIterationSecp256k1Books/Combo`：清空输出 → 启动对应 Kernel → 将结果拷回 CPU → 错误检查。
    - `doPrintOutput`：打印命中的 HASH/PRIV，并追加写入 `TEST_OUTPUT`。
    - `doFreeMemory`：释放全部 GPU/CPU 资源。
  - Kernel：`CudaRunSecp256k1Books` / `CudaRunSecp256k1Combo`
    - 生成私钥（SHA‑256），执行 `_PointMultiSecp256k1` 点乘；计算 Hash160（压缩与非压缩），取末 8 字节做 `_BinarySearch`，命中则把 HASH 与 PRIV 写入输出缓冲。
  - 设备函数：`_PointMultiSecp256k1`
    - 针对 16×16bit 分块的私钥，从 GTable 中选择非零项进行点加，末尾做模逆与归一化得到公钥。

- 底层数学与哈希（`GPU/GPUMath.h`, `GPU/GPUHash.h`）
  - GPUMath：大整数模运算、点加/倍点、快速二分 `_BinarySearch`，大量内联 PTX 优化。
  - GPUHash：SHA‑256 与 RIPEMD‑160，支持压缩/非压缩、公钥脚本（P2SH）等变体；针对 Books/Combo 提供专用装配。

## :heavy_check_mark: 适用/不适用场景
- 适用：私钥在进入 SECP 点乘前已被不可逆哈希（如 SHA‑256）处理，彼此之间无法由加法/增量推导（典型：脑钱包、部分助记词恢复、BIP39 变体等）。
- 不适用：可以从已知私钥“增量推导”出新私钥的场景（如纯递增“比特币谜题”、未哈希的 WIF、非哈希种子等），此时复用已算出的公钥更优。

## :gear: 构建与运行
### 环境要求
- Linux（建议 x86_64）
- NVIDIA GPU 与匹配的驱动
- CUDA Toolkit（默认路径 `/usr/local/cuda-11.7` 可在 `Makefile` 中修改）
- GMP 与 pthread（已在 Makefile 链接 `-lgmp -lpthread`）

### 快速开始
1) 克隆仓库后，确认 GPU 的 Compute Capability（见 NVIDIA 官网列表）。
2) 选择目标架构并构建：
   - T4（sm_75）：`make clean && make SMS=75`
   - RTX 30 系（sm_86）：`make clean && make SMS=86`
   - 多架构“胖二进制”（默认同时支持 75 与 86）：`make`
3) 运行内置样例：`./CudaBrainSecp`

构建说明：
- 项目使用 C++17（`std::filesystem`）。
- `Makefile` 通过 `SMS` 变量定义一个或多个 `sm_XX`，自动生成 `-gencode`；也可直接编辑 `CUDA` 路径。
- 若编译器较旧，可能需要在链接阶段额外加 `-lstdc++fs`。

### 常见输出（截断）
```
CudaBrainSecp Starting
HashMerge ...
GPUSecp Starting
GPU.deviceProp.name: Tesla T4
...
HASH: 3E546D0A... PRIV: D842151D...
...
```

## :wrench: 关键配置（`GPU/GPUSecp.h`）
- `BLOCKS_PER_GRID`、`THREADS_PER_BLOCK`：线程拓扑（需根据 GPU 调整以达成合适占用）。
- `MAX_LEN_WORD_PRIME`、`MAX_LEN_WORD_AFFIX`：词表最大长度（首字节存长度，数据紧随其后）。
- `AFFIX_IS_SUFFIX`：true 表示 Affix 为后缀，否则为前缀。
- `COUNT_INPUT_HASH`、`COUNT_INPUT_PRIME`：输入规模常量（与测试数据保持一致以节省寄存器）。
- `COUNT_COMBO_SYMBOLS`、`SIZE_COMBO_MULTI`：组合模式参数。
- `SIZE_CPU_STACK`、`SIZE_CUDA_STACK`：CPU/GPU 栈大小。

修改上述值后需要 `make clean && make` 重新编译。

## :file_folder: 测试数据与工具
- `TestBook/list_prime`、`TestBook/list_affix`：示例词表（Prime 小、Affix 大，有利于全局内存合并访问）。
- `TestHash/*`：多组 Hash160；运行时会合并并写出 `merged-sorted-unique-8-byte-hashes`。
- `TEST_OUTPUT`：命中结果输出（HASH 与对应 PRIV）。
- `addr_to_hash.py`：将地址转为 Hash160 的辅助脚本（Pieter Wuille 方案）。

## :warning: 注意事项
- 最小化容器中可能会看到 `setrlimit returned result = -1`，通常可忽略。
- 若出现 `no kernel image is available for execution on the device`，请使用与你 GPU 匹配的 `SMS` 重建。
- 针对不同显卡，合适的 `BLOCKS_PER_GRID`/`THREADS_PER_BLOCK` 能显著影响吞吐。

## :coffee: 致谢与参考
- [Jean Luc PONS VanitySearch/SECP 库](https://github.com/JeanLucPons/VanitySearch)
- [CUDA Hashing Algorithms Collection](https://github.com/mochimodev/cuda-hashing-algos)
- [Secp256k1 Calculator](https://github.com/MrMaxweII/Secp256k1-Calculator)
- [PrivKeys Database](https://privatekeys.pw/)（仅用于生成样例，勿输入真实私钥）
- `addr_to_hash.py` by Pieter Wuille

## :grey_question: 常见问题（FAQ）
- 目标是什么？
  - 为遗忘或缺失部分信息的钱包提供 GPU 端高吞吐的检索能力；同时也是学习 CUDA 与 SECP 的实践项目。
- 只做 GPU 点乘可以吗？
  - 可以。你可在自定义 kernel 中调用 `_PointMultiSecp256k1` 并从全局内存传入 GTable。
- 为什么是 CUDA 而不是 OpenCL？
  - 依赖的数学实现与优化面向 CUDA，迁移到 OpenCL 成本高且性能难以保证。
- 目前支持哪些币？
  - 所有使用 secp256k1 公钥的币（BTC/BCH/ETH/LTC/DOGE/DASH 等）。ETH 的地址哈希流程与 BTC 不同，需要调整哈希部分。

如果这个项目对你有帮助，也欢迎打赏支持原作者（见其主页）。
