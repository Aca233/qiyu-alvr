# Qiyu 客户端方案（基于最新 ALVR v20.14.1）

> 目标：交付一个"类似 hallychou/ALVR-QIYU 的方案，但基于最新 ALVR（v20.14.1）"的奇遇 VR 客户端。
> 一句话结论：**我们现在做的 Path B 本质上就是这件事**——把 hallychou 的 Qiyu 客户端代码（Qiyu SDK 桥接）搬到 ALVR 最新的串流核心（v20.14.1）之上。

---

## 一、hallychou 方案的真实结构（已扒源码确认）

- 仓库 `hallychou/ALVR-QIYU` = **整棵 ALVR fork**，基于 **ALVR 19.1.1**（Cargo.toml 实测 `version = "19.1.1"`）。
- 它的 Qiyu 桥接代码在 `android/app/src/main/cpp/cpp_main.cpp`（47 KB），配合 `QiyuApi.h` / `QYRenderTarget.cpp` / `QYUtil.cpp` 以及 `jniLibs/`（Qiyu 原生 `.so`）。
- 工作方式：`cpp_main.cpp` 调用 ALVR 的 C API（`alvr_*`）做串流/事件/追踪，调用 Qiyu 原生 SDK（`qiyu_*`）做头显/控制器/渲染提交。

**关键事实**：hallychou 用的 ALVR 是 **19.1.1**；我们用的核心是 **v20.14.1**（最新 release tag）。两者的 ALVR C ABI 差异巨大（见《字段对照表》第二节），所以不能直接套他的 `cpp_main.cpp`，必须按 20.14.1 重写 ALVR 那一层——这正是我们 Path B 已经完成的。

---

## 二、两种可行架构

### 方案 A：Path B（当前采用，推荐）✅
- **取**：ALVR `alvr_client_core`（Rust 串流核心，v20.14.1）+ cbindgen 生成的 C 头文件。
- **取**：hallychou 的 Qiyu 客户端代码（`app/src/main/cpp/` 全套 Qiyu 桥接 + `jniLibs/`）。
- **改**：仅重写 `cpp_main.cpp` 中 ALVR 那一层（C ABI 对齐 v20.14.1），Qiyu 层原样保留。
- **优点**：核心最新（20.14.1 > 19.1.1 约一个大版本）；只背核心，干净、好维护；CI 已搭好。
- **现状**：`cpp_main.cpp` 已与 v20.14.1 权威定义逐字段核对通过，待重跑 CI 出 APK。

### 方案 B：全树 fork `alvr/client_openxr`（更重，备选）
- Fork ALVR v20.14.1 整个仓库，在 `alvr/client_openxr`（官方 Android 客户端，含完整的 OpenXR/EGL/WGPU 渲染、设置 UI、Dashboard 通信）里注入 Qiyu 支持。
- **优点**：能复用 ALVR 官方客户端全部能力（更完整的渲染管线、设置页、错误提示）。
- **代价**：要把 Qiyu 的 `qiyu_*` 提交/预测/控制器逻辑塞进 `client_openxr` 的渲染循环（hallychou 在 19.1.1 上曾这么做过，但 20.14.1 的 `client_openxr` 已大幅重构为 wgpu，改动量很大）；升级 ALVR 时合并冲突多。
- **结论**：除非你需要 ALVR 官方客户端的完整 UI/渲染能力，否则方案 A 更优。

---

## 三、已完成的迁移映射（hallychou 19.1.1 → 我们 20.14.1）

| hallychou 19.1.1 调用 | 我们 20.14.1 对应 | 备注 |
|---|---|---|
| `alvr_path_string_to_hash` | `alvr_path_string_to_id` | 重命名 |
| `alvr_initialize(vm, ctx)` | `alvr_initialize_android_context` + `alvr_initialize(caps)` | 拆成两步 + 能力结构体 |
| `alvr_get_prediction_offset_ns()` | 自预测（Qiyu `qiyu_PredictDisplayTime`） | 已移除 |
| `alvr_send_views_config(fov, ipd)` | `alvr_send_view_params(AlvrViewParams[2])` | 结构变更 |
| `alvr_send_tracking(ts, data, n, L, R)` | `alvr_send_tracking(ts, data, n, nullptr, nullptr)` | 去 OculusHand 参 |
| `alvr_start_stream_opengl(handles, n)` | `alvr_start_stream_opengl(AlvrStreamConfig)` | 配置结构体化 |
| `alvr_resume_opengl(w,h,handles,...)` | `alvr_resume_opengl(w,h,const uint32_t**,n)` + `alvr_resume()` | 拆分 |
| `ts = alvr_get_frame(&buf)` | `alvr_get_frame(&tsNs, &buf)` | 返回值改 bool |
| （无） | `alvr_create_decoder(AlvrDecoderConfig)` + `alvr_get_decoder_config` | 20.14.1 新增 |
| （无） | `alvr_send_active_interaction_profile` | 20.14.1 新增 |
| （无） | `alvr_report_compositor_start` | 20.14.1 新增 |

---

## 四、构建 / CI（已就绪，直接复用）

- `build.sh`：克隆 ALVR `v20.14.1`（含 openvr 子模块）→ `cargo ndk` 编 `alvr_client_core` → cbindgen 生成 `alvr_client_core.h` → 暂存到 `build/alvr_client_core`。
- `.github/workflows/build.yml`：装 SDK/NDK r25b/CMake → 跑 `build.sh` → `gradle assembleRelease` 出 APK。
- 关键版本锁：`ALVR_TAG=v20.14.1`、`NDK_VERSION=25.1.8937393`、`GRADLE_VERSION=7.6.1`、`CARGO_NDK_VERSION=4.1.2`。
- ⚠️ 升级 ALVR 时务必同步更新 `build.sh` 的 `ALVR_TAG` 并**重新核对 `cpp_main.cpp` 的 C ABI**（ALVR 每个 release 的 C API 都可能有变）。

---

## 五、真机联调验证清单（出包后）

- [ ] 头显能发现 PC 端 ALVR Server 并连接
- [ ] 串流画面正常（H264/HEVC 解码出帧，无花屏/黑屏）
- [ ] 控制器按键/摇杆映射到 ALVR（binary/scalar 分支）
- [ ] 手柄震动（`ALVR_EVENT_HAPTICS` → `qiyu_StartControllerVibration`）
- [ ] 头显/手柄 6DoF 追踪与 ATW 预测
- [ ] 视场角/瞳距（`alvr_send_view_params`）正确
- [ ] Foveated encoding 开关行为（必要时补全 foveation 中心/边缘参数）
- [ ] 电池电量上报

---

## 六、下一步建议

1. **推 `cpp_main.cpp`（binary/scalar 修复）重跑 CI**，预期出 APK。
2. 出包后在奇遇真机跑第五节清单，把运行时问题反馈回来。
3. 若需 ALVR 官方客户端完整 UI，再评估方案 B（全树 fork `client_openxr`）；否则维持方案 A。
