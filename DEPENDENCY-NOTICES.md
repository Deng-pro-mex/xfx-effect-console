# 离线翻译运行时第三方依赖 notice 审计

基准日期：2026-11-04。审计对象是**已装到本机的候选组件**`XFXOfflineTranslation`，不是发布物、不是安装器、也不是经过安全签名或法律审查的发行包。

结论先行：**当前候选未达到"完整可再分发"标准。** `runtime/transformers.js` 内联了若干上游组件，随包 notice 未逐一覆盖，其中 `guid-typescript` 上游根本未发布许可文件。本文件记录证据、缺口与需要补齐的动作；它不构成"许可已合规"的声明。

## 审计对象与哈希

| 文件 | 字节 | SHA256 |
| --- | --- | --- |
| `runtime/transformers.js` | 1,729,619 | `3d4c537f5c2cc169834058bde11d5d25fea910f00d92a32ec979b833461bb3b4` |
| `runtime/ort-wasm.wasm` | 9,223,228 | `bbdcb6b3c7d294577d806077630460be4e13ca35a345acb5e81188e9649fa74a` |
| `runtime/ort-wasm-simd.wasm` | 10,014,674 | `9bd07bababc65f53d061f457233eeae501be7ceb8a2adb9eef52d87fe776d865` |
| `node.exe` | 91,694,408 | `3331e1ffe19874215472217c5e94f5a0c6d8e18c4ac7111d3937aa0ad5e9b4a5` |
| `LICENSES/onnxruntime-v1.14.0-ThirdPartyNotices.txt` | 299,762 | `cc63920441c5681cfb1663caf1de1858349b517fc1bed9ca669a85bc526cd13e` |
| `LICENSES/transformers-APACHE2.txt` | 11,358 | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |

`runtime/transformers.js` **与上游 `@xenova/transformers@2.17.2` 的 `dist/transformers.js` 逐字节相同**（本机 `npm pack` 取得的上游 tarball，SHA256 相同），因此内联许可证的处置责任与上游发行物一致，不是本仓库自行打包造成的偏差。两个 WASM 与 `onnxruntime-web@1.14.0` 的 `dist/` 同名文件亦逐字节相同。`LICENSES/transformers-APACHE2.txt` 与上游 `LICENSE` 规范化后相同，且与 flatbuffers、long 的 Apache-2.0 全文逐字节相同。

## 实际内联进 `transformers.js` 的组件

该文件是 webpack 产物，共 35 个模块：16 个 `./src/*`（Transformers.js 自身）、10 个 `onnxruntime-common/dist/lib/*`、1 个 `onnxruntime-web/dist/ort-web.min.js`、1 个 `@huggingface/jinja/dist/index.js`，以及 7 个 `(ignored)` 空桩（`onnxruntime-node`、`sharp`、`fs`、`path`、`url`）。

| 组件 | 版本 | 许可 | 并入方式 | 判定依据 |
| --- | --- | --- | --- | --- |
| Transformers.js (`@xenova/transformers`) | 2.17.2 | Apache-2.0 | 顶层本体，16 个自有模块 | 上游 `dist/transformers.js` 逐字节相同 |
| ONNX Runtime Web | 1.14.0 | MIT | `ort-web.min.js` 作为单一 block，保留 `/*! ONNX Runtime Web v1.14.0 … MIT License */` 横幅 | 横幅存在；上游 dist 与随包文件指纹一致 |
| ONNX Runtime Common | 1.14.0 | MIT | 10 个独立 block | 10/10 均保留 `Copyright (c) Microsoft Corporation` 头 |
| flatbuffers | 1.12.0 | Apache-2.0 | 内联在 ort-web 预构建产物内 | `ByteBuffer` 43 处、`flatbuffers` 24 处；上游 dist 同源 |
| long.js | 4.0.0 | Apache-2.0 | 同上 | `LongBits` 28 处、`division by zero` 常量同上游 |
| guid-typescript | 1.0.9 | ISC（仅 `package.json` 声明） | 同上 | `isGuid` 3 处，与上游 dist 一致 |
| onnx-proto | 4.0.4 | MIT | 同上 | protobuf.js 运行时内联（见下行） |
| protobuf.js | 6.x（具体版本不可由产物断定） | BSD-3-Clause | 同上 | `LongBits`/`Writer`/`Reader` 运行时存在；`protobuf` 字面量被压缩移除（0 处） |
| @huggingface/jinja | 0.2.2 | MIT | 独立 block | 与上游 0.2.2 dist 仅差 webpack 导出壳 |
| platform.js | 1.3.6 | MIT | **未并入** | `Opera Mini`/`Windows Phone`/`Silk`/`BlackBerry`/`webOS`/`navigator.platform` 在随包文件与上游 dist 中均为 0 处；声明的依赖未被实际打进产物 |
| sharp | ^0.32.0 | Apache-2.0 | **未并入**（`sharp (ignored)` 空桩） | 图片解码路径未编译进产物 |
| onnxruntime-node | 1.14.0 | MIT | **未并入**（`onnxruntime-node (ignored)` 空桩） | 走 WASM 后端 |

Adobe After Effects SDK、MSVC 运行库、ONNX 模型权重与 `node.exe` 属另外的来源与许可链条，不在本表内展开。

## 随包 notice 的覆盖缺口

已核对 `LICENSES/onnxruntime-v1.14.0-ThirdPartyNotices.txt`（5,787 行）：

- 已覆盖：`google/flatbuffers`（第 3024 行起，含 Apache-2.0 全文）、`huggingface/transformers`（第 4070 行）、`protocolbuffers/protobuf`（第 92 行，C++ 项目的 BSD 文本）。
- **未覆盖**：`guid-typescript`、`long.js`、`platform.js`、`onnx-proto`、`protobuf.js`（npm 包本体）、`@huggingface/jinja` —— 逐个关键词均为 0 命中。该文件里的 `protobuf` 条目是 Google 的 C++ 实现，与内联的 protobuf.js 不是同一作品，不能互相顶替。
- `long.js`、`flatbuffers`、`onnx-proto` 的上游发行包里**没有 NOTICE 文件**，因此不存在"漏附 NOTICE"，但也意味着 Apache-2.0 的归属信息只能靠自行保留许可证全文。
- `@huggingface/jinja` 的 `dist/index.js` 自身**不含任何版权或许可注释**。

## 已补入源码树的许可证全文

为使"随包只给 ThirdPartyNotices 不够"这件事可复核，已在 `native/third_party/` 保留逐字上游文本，每份文件顶部附来源 URL、取得日期与并入方式说明（该说明不属于上游文本）：

| 文件 | 组件 | 上游来源 |
| --- | --- | --- |
| `flatbuffers-LICENSE.txt` | flatbuffers 1.12.0 | npmjs.com/package/flatbuffers/v/1.12.0 |
| `long-LICENSE.txt` | long 4.0.0 | npmjs.com/package/long/v/4.0.0 |
| `guid-typescript-LICENSE.txt` | guid-typescript 1.0.9 | npmjs.com/package/guid-typescript/v/1.0.9（**无上游文件**，见下） |
| `onnx-proto-LICENSE.txt` | onnx-proto 4.0.4 | npmjs.com/package/onnx-proto/v/4.0.4 |
| `protobufjs-LICENSE.txt` | protobuf.js 6.11.4（范围内取得） | npmjs.com/package/protobufjs/v/6.11.4 |
| `huggingface-jinja-LICENSE.txt` | @huggingface/jinja 0.2.2 | npmjs.com/package/@huggingface/jinja/v/0.2.2 |
| `platform-LICENSE.txt` | platform.js 1.3.6（未并入，留存备查） | npmjs.com/package/platform/v/1.3.6 |

## 未解除的问题

1. **`guid-typescript` 归属信息缺失。** 上游 1.0.9 的 tarball 只有 `dist/`、`package.json`、`README.md`；`package.json` 声明 `"license":"ISC"`，源码里没有任何版权行，作者字段只有 `nicolas`。故 `guid-typescript-LICENSE.txt` 里的 ISC 文本是通用模板，**版权人字符串未知**，不是上游原文的复制。这属于未解决的署名缺口。
2. **内联的 protobuf.js 具体发行版无法由产物断定。** `onnx-proto@4.0.4` 声明 `protobufjs ^6.8.8`，6.x 各版 BSD-3-Clause 文本一致，故许可证本身不受影响；但若将来要求逐版本 SPDX 归因，需要上游锁定版本。
3. **`onnxruntime-web@1.14.0` 声明了 `platform@^1.3.6` 却未实际打包其代码。** 这不产生署名义务，但说明"依赖清单"与"实际产物"不能互相代替。
4. **`LICENSES/PROVENANCE.txt` 现有措辞仍然准确**（"can include transitive components … notices may be incomplete … a complete audit is REQUIRED"）。本次审计把该措辞落到了具体组件，但并没有把结论改成"已完整"。

## 发布前必须补齐（按优先级）

1. 明确 `guid-typescript` 的版权归属：联系上游或改用不含该依赖的构建；在此之前不得声称 notice 完整。
2. 把上表 7 份文本并入候选组件的 `LICENSES/`，或在 `PROVENANCE.txt` 内逐项列出组件＋版本＋许可＋来源 URL。
3. 用锁定版本的 `package-lock.json` 复现 webpack 构建，使内联组件版本可追溯（当前产物只能证明"与上游 2.17.2 dist 相同"）。
4. 由具备资质的人复核 Apache-2.0 / MIT / BSD-3-Clause / ISC 的实际义务（尤其 Apache-2.0 的 NOTICE 传递与修改声明），再决定是否可分发。

本文件由离线静态分析写成：未联网、未运行打包流程、未重新构建 bundle，也未替任何第三方作法律判断。
