# MicroTech Built-in Apps

本仓库提供 MicroTech 固件随镜像发布的 LVGL 内置应用。各应用使用 App Manager 生命周期接口创建和销毁页面，通过中间件服务读取状态或发起操作，不直接拥有硬件驱动。

## 目录结构与应用

- `common/`：统一页面、标题栏、操作行和值行，以及异步返回/应用跳转辅助函数。
- `home_app/`：显示本地时间、时间质量和电源快照，提供常用入口。
- `menu_app/`：列出 Home、Applications、Settings 和 Setup 内置应用。
- `settings_app/`：提供亮度调节、电源状态/熄屏入口及设备信息页面。
- `setup_app/`：通过自有 Wi-Fi session 完成扫描、连接、断开和取消，并过滤非本次操作的事件快照。
- `tests/host/`：共用导航和 Setup Wi-Fi adapter 的宿主测试及最小依赖 fake。

每个应用以 `BUILTIN_APP_EXPORT` 导出描述符；`apps` 组件使用 `WHOLE_ARCHIVE`，App Manager 的链接脚本负责保留和发现这些描述符。新增应用时应在独立目录中实现生命周期 handler，并显式加入根 `CMakeLists.txt` 的 `APP_SRCS`，不要使用递归 glob。

## ESP-IDF 集成

本仓库依赖 App Manager 和 MicroTech 中间件。将相关组件目录加入项目根 `CMakeLists.txt`：

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/app_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/middleware/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
)
```

在固件入口组件中声明 `PRIV_REQUIRES apps`，确保内置应用归档参与最终链接。工程还需提供 `app_core`、`app_theme`、`event_bus`、`wifi_service`、`time_service`、`power_service`、`mt_log` 和 LVGL；具体依赖以根 `CMakeLists.txt` 为准。组件要求 ESP-IDF 5.0 或更高版本。

## 宿主测试

```sh
cmake -S tests/host -B /tmp/mt-apps-host -G Ninja \
    -DAPPS_SANITIZER=none
cmake --build /tmp/mt-apps-host
ctest --test-dir /tmp/mt-apps-host --output-on-failure
```

`APPS_SANITIZER` 还支持 `address`（ASan/UBSan）和 `thread`（TSan）。当前宿主测试验证应用 ID 异步持有、队列失败，以及 Wi-Fi session、操作过滤、回调和可重试清理；不替代 ESP32-S3 上的界面、无线和内存验证。

## 设计与修改边界

页面资源必须与 App Manager 生命周期对应，事件订阅和服务 session 在暂停/停止时按逆序释放；释放失败应上报并保留可重试状态。事件总线 UI 回调只消费有效快照，Wi-Fi 密码使用后必须清零。共用 UI 只放跨应用稳定能力，避免应用状态互相耦合。遵循主工程 `doc/code-style.md`，不得修改 ESP-IDF、`managed_components/` 或中间件实现来规避本层问题。

## 许可证

本项目采用 [MIT License](LICENSE)。
