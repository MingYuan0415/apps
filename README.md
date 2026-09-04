# MicroTech Built-in Apps

本仓库提供 MicroTech 固件随镜像发布的 LVGL 内置应用。每个挂载页面拥有独立 Screen，各应用通过 App Manager 生命周期管理 UI 和前台会话，通过中间件服务读取状态或发起操作，不直接拥有硬件驱动。

## 目录结构与应用

- `common/`：统一 368 x 448 页面骨架、标题栏、导航/命令行、值行和语义状态，保留异步导航 API。
- `home_app/`：顶部 Wi-Fi/蓝牙/电池状态条（Wi-Fi 与电池为 LVGL 矢量绘制），中央矢量模拟表盘随 `time_service` 走时并标注数字时间、日期与时间源质量，天气横幅显示实况图标、当前温度与今日高低温/湿度（点击进天气），底部时钟/录音/水平仪/设置四枚圆环瓦片，时钟瓦片在倒计时或专注运行时变为进度环；最近任务由 App Manager 的系统任务切换器提供。
- `menu_app/`：按注册 descriptor 构建应用目录，统一展示图标、名称和功能摘要。
- `clock_app/`：时钟枢纽页聚合四张卡片（倒计时、时长选择、秒表、专注），子页共享 App-scope
选中时长与运行会话；专注支持周期模式。
- `recorder_app/`：双声道录音主页与文件页；录制、播放、删除与 `.part` finalize 全部经由
`recorder_service` worker，页面只提交命令并轮询 generation 快照。
- `level_app/`：IMU 气泡水平仪；校准偏移经 `chore_service` 短任务读写并跨挂载保留，
恢复出厂后重载为零。
- `diagnostics_app/`：硬件自检页，由设置页五次点击入口打开，`HIDDEN` 标志使其不出现在普通应用目录。
- `settings_app/`：提供亮度、固定熄屏/待机延迟选项、电源详情、连接管理、时间状态、SD 存储状态、运行时固件描述及恢复出厂设置两步确认页。恢复请求只有在 reset journal 持久化成功后才进入不可重复点击的重启等待状态；保存失败时页面保留并允许重试。
- `setup_app/`：开启限时 BLE 绑定窗口，显示六位 Numeric Comparison 并在本机确认，同时管理已保存网络的断开、重连、自动连接和忘记操作；网络选择与密码输入在手机侧完成，设备只显示六位 Numeric Comparison 并本机确认。
- `weather_app/`：展示当前天气、预警、24 小时和 7 日预报，包含预警列表与详情页；只消费 `weather_service` 快照，不执行网络、JSON 或缓存 I/O。
- `tests/host/`：共用导航、天气图标映射表、恢复出厂确认页、Setup Wi-Fi adapter 与 apps 持久化的宿主测试及最小依赖 fake。

每个 App 的只读资源必须放在自己的 `assets/` 目录，并在该 App 的
`resource_manifest.cmake` 中逐条声明；禁止递归 glob。图片资源以 SVG 为唯一入库源
（不提交 PNG）：构建期由 `cmake/mt_app_resources.cmake` 统一执行
`tools/asset_pipeline/svg2png.py`（PyMuPDF）栅格化为 RGBA PNG，再经锁定的
LVGLImage.py 转 RGB565A8 BIN 打包进唯一的 `res` 分区；manifest 记录的宽/高即导出
尺寸，同一 SVG 可登记多条尺寸记录，kind 取值 `SVG|PNG|FONT`。SVG 必须写入终色
（不允许 `currentColor`）并带 `viewBox`。运行时通过 `app_image_ids.h` 中的语义 ID
查询，App descriptor 的 `icon_id` 指向同一资源表，缺失时由系统界面回退到 LVGL
symbol；新增资源需要同时更新 manifest、语义 ID 和 `tests/resources`，改动 manifest
后执行 `idf.py reconfigure`。天气状况图标为 QWeather Icons（MIT）的 `N-fill.svg`
vendor 源，随附 `weather_app/assets/qweather-icons-LICENSE.txt`。

每个应用以普通 `const` Page definition 描述 typed ops 及私有内存大小，并在 App 私有 route 表中显式绑定 `page_id`、definition 和 route `user_data`。App descriptor（`APP_MANAGER_APP_EXPORT` 及其 `APP_MANAGER_APP_EXPORT_META` 变体）进入 `.app_manager_apps` 链接段；`apps` 组件使用 `WHOLE_ARCHIVE`，App Manager 的链接脚本负责保留和发现该段。新增应用时应在独立目录中实现生命周期 ops，并显式加入根 `CMakeLists.txt` 的 `APP_SRCS`，同时提供图标 manifest，不要使用递归 glob。

页面只声明实际需要的生命周期阶段，无需为未使用阶段编写空回调。例如静态页面只需
`.mount` 和 `.unmount`；需要前台刷新时再增加 `.resume` 和 `.pause`；需要跨挂载周期保留
状态时才增加 `.start` 或 `.stop`。新应用使用 typed ops，raw handler 仅用于兼容旧页面。

页面 UI 只在 `ONMOUNT/ONUNMOUNT` 中创建和销毁，根对象必须挂到 Page Screen。Timer、事件订阅、worker 和服务会话只在 `ONRESUME/ONPAUSE` 中启停；清理失败须保留资源 handle 并允许生命周期重试。每个页面私有状态都以静态断言约束在 `APP_MANAGER_PAGE_STATE_BYTES` 内。所有产品页面使用类型化 Page ops，预警详情通过 Typed Blob 接收值复制的 alert key；新增页面沿用同一生命周期边界。

Weather 页面恢复前台时订阅 `WEATHER_SERVICE_MSG` 并 acquire 当前不可变快照，暂停时
先退订再 release；事件只触发按 generation 重取，不复制小时、逐日或预警数组。预警详情
从版本化 Typed Blob 的 START/NEWINTENT 参数读取 alert key，不依赖跨页面可变全局。详情页
的临时格式化缓冲位于 UI worker 任务栈。图片按语义 ID 查询 mmap 描述符，资源缺失时使用 LVGL
symbol，不在页面状态中保存文件路径或可变图片 payload。

录音应用使用 `recorder_service` 写入 16 kHz/16-bit/双声道 WAV。录音、播放、删除和
`.part` 到 `.wav` 的 finalize 均由 service worker 异步执行，页面只提交命令并读取
generation snapshot；服务不可用或 finalize 失败时页面保持可见并允许重试。
水平仪使用 IMU 加速度计算横纵倾角，校准读写通过 `chore_service` 的短任务完成，页面只消费结果；校准由 Apps storage helper 持久化，`apps_factory_reset_persisted_state()` 删除后重新加载为零偏移。诊断页面通过设置中的五次点击入口打开，不出现在普通应用目录。

## ESP-IDF 集成

本仓库依赖 App Manager 和 MicroTech 中间件。将相关组件目录加入项目根 `CMakeLists.txt`：

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/app_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/middleware/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
)
```

在固件入口组件中声明 `PRIV_REQUIRES apps`，确保内置应用归档参与最终链接。工程还需提供 `app_core`、`app_theme`、`event_bus`、`chore_service`、`connectivity_manager`、`device_link_service`、`factory_reset_service`、`time_service`、`timer_service`、`power_service`、`imu_service`、`audio_service`、`sd_storage_service`、`recorder_service`、`weather_service`、`onboarding_service`、`nv_storage`、`freertos`、`heap`、`fatfs`、`esp_app_format`、`esp_hw_support`、`mt_log` 和 LVGL；具体依赖以根 `CMakeLists.txt` 为准。组件要求 ESP-IDF 5.1 或更高版本，并启用外部 RAM task stack 支持。

## 宿主测试

```sh
cmake -S tests/host -B /tmp/mt-apps-host -G Ninja \
    -DAPPS_SANITIZER=none
cmake --build /tmp/mt-apps-host
ctest --test-dir /tmp/mt-apps-host --output-on-failure
```

`APPS_SANITIZER` 还支持 `address`（ASan/UBSan）和 `thread`（TSan）。当前宿主测试验证 RUN/BACK/OPEN_PAGE 请求、统一命令池准入失败、ID 值复制、completion 恰好一次、天气图标映射表覆盖全部 20 个主图与 20 个小图资源且无死资源、恢复出厂确认页的失败重试与成功防重复、level 校准 save/load 与恢复出厂清除，以及保存网络操作过滤、快照回调和取消清理；不替代 ESP32-S3 上的界面、Numeric Comparison 确认、BLE、无线和内存验证。

音频、存储和时钟 worker adapter 的故障恢复测试，以及产品页面的跨层生命周期测试位于主工程 `tests/integration/`，通过 `CROSS_LAYER_SANITIZER` 分别运行普通、ASan/UBSan 和 TSan 配置。

## 设计与修改边界

页面资源必须与 App Manager 生命周期对应，稳定后台页不得保留 LVGL 对象、timer、事件订阅或页面 worker；释放失败应上报并保留可重试状态。Wi-Fi 连接和系统 SNTP 可跨页面存在，分别由 `connectivity_manager` 和 `time_service` 所有。UI worker 只投递音频、文件和 RTC 命令并读取线程安全快照，不直接执行 PCM、文件 I/O 或 RTC I2C。遵循主工程 `doc/code-style.md`，不得修改 ESP-IDF、`managed_components/` 或中间件实现来规避本层问题。

## 许可证

本项目采用 [MIT License](LICENSE)。
