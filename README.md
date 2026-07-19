# MicroTech Built-in Apps

本仓库提供 MicroTech 固件随镜像发布的 LVGL 内置应用。每个挂载页面拥有独立 Screen，各应用通过 App Manager 生命周期管理 UI 和前台会话，通过中间件服务读取状态或发起操作，不直接拥有硬件驱动。

## 目录结构与应用

- `common/`：统一页面、标题栏、操作行和值行，以及基于统一导航命令池的异步返回、应用和静态页面跳转辅助函数。
- `home_app/`：显示本地时间、时间质量和电源快照，提供常用入口。
- `menu_app/`：列出 Home、Applications、Settings 和 Setup 内置应用。
- `settings_app/`：提供亮度调节、电源状态/熄屏入口及设备信息页面。
- `setup_app/`：通过自有 Wi-Fi session 完成扫描、连接、断开和取消，并过滤非本次操作的事件快照。
- `tests/host/`：共用导航和 Setup Wi-Fi adapter 的宿主测试及最小依赖 fake。

每个应用以 `APP_MANAGER_APP_EXPORT` 导出应用描述符，并以 `APP_MANAGER_PAGE_EXPORT` 导出根页面和其他静态页面；`apps` 组件使用 `WHOLE_ARCHIVE`，App Manager 的链接脚本负责保留和发现两个描述符段。新增应用时应在独立目录中实现生命周期 handler，并显式加入根 `CMakeLists.txt` 的 `APP_SRCS`，不要使用递归 glob。

页面 UI 只在 `ONMOUNT/ONUNMOUNT` 中创建和销毁，根对象必须挂到 `app_manager_this_page_screen()`。Timer、事件订阅、输入和 Wi-Fi session 只在 `ONRESUME/ONPAUSE` 中启停；清理失败须保留资源 handle，并从 `ONPAUSE` 或 `ONSTOP` 上报以便重试。`ONSTART/ONSTOP` 只管理非视觉保留状态，`ONNEWINTENT` 用于消费更新后的 Typed Blob 参数。

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

`APPS_SANITIZER` 还支持 `address`（ASan/UBSan）和 `thread`（TSan）。当前宿主测试验证 RUN/BACK/OPEN_PAGE 请求、统一命令池准入失败、ID 值复制、completion 恰好一次，以及 Wi-Fi session、操作过滤、回调和可重试清理；不替代 ESP32-S3 上的界面、动画、无线和内存验证。

## 设计与修改边界

页面资源必须与 App Manager 生命周期对应，稳定后台页不得保留 LVGL 对象、timer、事件订阅或活动 session；释放失败应上报并保留可重试状态。事件总线 UI 回调只消费有效快照，Wi-Fi 密码使用后必须清零。共用 UI 只放跨应用稳定能力，避免应用状态互相耦合。遵循主工程 `doc/code-style.md`，不得修改 ESP-IDF、`managed_components/` 或中间件实现来规避本层问题。

## 许可证

本项目采用 [MIT License](LICENSE)。
