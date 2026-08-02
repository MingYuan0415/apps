# MicroTech Built-in Apps

本仓库提供 MicroTech 固件随镜像发布的 LVGL 内置应用。每个挂载页面拥有独立 Screen，各应用通过 App Manager 生命周期管理 UI 和前台会话，通过中间件服务读取状态或发起操作，不直接拥有硬件驱动。

## 目录结构与应用

- `common/`：统一 368 x 448 页面骨架、标题栏、导航/命令行、值行和语义状态，保留异步导航 API。
- `home_app/`：显示本地时间与质量、电量/供电、Wi-Fi 和 SD 挂载状态，提供演示中心、配网和设置入口。
- `menu_app/`：作为演示中心，包含运动传感、音频、SD 存储和时间/RTC 四个静态页；音频、文件和 RTC 操作均由页面自有 worker 执行。
- `settings_app/`：提供亮度、固定熄屏/待机延迟选项、电源详情及运行时固件描述。
- `setup_app/`：通过二维码开启限时 BLE 手机配网，并管理已保存网络的断开、重连、自动连接和忘记操作；不再在设备侧扫描、选择网络或输入密码。
- `tests/host/`：共用导航和 Setup Wi-Fi adapter 的宿主测试及最小依赖 fake。

每个应用以普通 `const` Page definition 描述 handler 和私有内存大小，并在 App 私有 route 表中显式绑定 `page_id`、definition 和 route `user_data`。只有 `APP_MANAGER_APP_EXPORT` 产生的 App descriptor 进入 `.app_manager_apps` 链接段；`apps` 组件使用 `WHOLE_ARCHIVE`，App Manager 的链接脚本负责保留和发现该段。公共 Page definition 可被多个 App route 引用，但未显式绑定的 App 不能导航到它，也不支持运行时自由挂载。新增应用时应在独立目录中实现生命周期 handler，并显式加入根 `CMakeLists.txt` 的 `APP_SRCS`，不要使用递归 glob。

页面 UI 只在 `ONMOUNT/ONUNMOUNT` 中创建和销毁，根对象必须挂到 `app_manager_this_page_screen()`。Timer、事件订阅、worker 和服务会话只在 `ONRESUME/ONPAUSE` 中启停；清理失败须保留资源 handle，从 `ONPAUSE` 或 `ONSTOP` 上报并允许后续生命周期重试。音频、存储和时钟 worker 的 task stack 必须从 PSRAM 分配，并由页面所有者同步删除，避免占用 LCD 与 I2S 共用的内部 DMA heap。每个页面私有状态都以静态断言约束在 2728 B 以内。

SD 自检仅在已挂载的 `/sdcard` 下以 `O_EXCL` 创建本次独占的 4 KiB 临时文件，分块写入、读回校验并删除；不枚举、覆盖或格式化用户文件。未挂载时应用只提示插卡后重启，不调用存储服务的 init/deinit。

## ESP-IDF 集成

本仓库依赖 App Manager 和 MicroTech 中间件。将相关组件目录加入项目根 `CMakeLists.txt`：

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/app_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/middleware/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
)
```

在固件入口组件中声明 `PRIV_REQUIRES apps`，确保内置应用归档参与最终链接。工程还需提供 `app_core`、`app_theme`、`event_bus`、`connectivity_manager`、`time_service`、`power_service`、`imu_service`、`audio_service`、`sd_storage_service`、`freertos`、`heap`、`fatfs`、`esp_app_format`、`esp_hw_support`、`mt_log` 和 LVGL；具体依赖以根 `CMakeLists.txt` 为准。组件要求 ESP-IDF 5.1 或更高版本，并启用外部 RAM task stack 支持。

## 宿主测试

```sh
cmake -S tests/host -B /tmp/mt-apps-host -G Ninja \
    -DAPPS_SANITIZER=none
cmake --build /tmp/mt-apps-host
ctest --test-dir /tmp/mt-apps-host --output-on-failure
```

`APPS_SANITIZER` 还支持 `address`（ASan/UBSan）和 `thread`（TSan）。当前宿主测试验证 RUN/BACK/OPEN_PAGE 请求、统一命令池准入失败、ID 值复制、completion 恰好一次，以及保存网络操作过滤、快照回调和取消清理；不替代 ESP32-S3 上的界面、二维码、BLE、无线和内存验证。

音频、存储和时钟 worker adapter 的故障恢复测试，以及四个演示页的跨层生命周期测试位于主工程 `tests/integration/`，通过 `CROSS_LAYER_SANITIZER` 分别运行普通、ASan/UBSan 和 TSan 配置。

## 设计与修改边界

页面资源必须与 App Manager 生命周期对应，稳定后台页不得保留 LVGL 对象、timer、事件订阅或页面 worker；释放失败应上报并保留可重试状态。Wi-Fi 连接和系统 SNTP 可跨页面存在，分别由 `connectivity_manager` 和 `time_service` 所有。UI worker 只投递音频、文件和 RTC 命令并读取线程安全快照，不直接执行 PCM、文件 I/O 或 RTC I2C。遵循主工程 `doc/code-style.md`，不得修改 ESP-IDF、`managed_components/` 或中间件实现来规避本层问题。

## 许可证

本项目采用 [MIT License](LICENSE)。
