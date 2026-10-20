# MicroTech Built-in Apps

本仓库提供 MicroTech 固件随镜像发布的 LVGL 内置应用。每个挂载页面拥有独立 Screen，各应用通过 App Manager 生命周期管理 UI 和前台会话，通过中间件服务读取状态或发起操作，不直接拥有硬件驱动。

## 目录结构与应用

- `common/`：统一 368 x 448 页面骨架（64 px 标题栏，36 px 标题）、圆形图标底的导航/命令行、值行和语义状态，保留异步导航 API；分组列表（`app_ui_group_*`，行间 1 px 分隔）、胶囊按钮（`app_ui_button_set_kind` 主要/次要/危险）、分段控件（`app_ui_chip_*`）、状态胶囊（`app_ui_add_pill`）、页内模态底部面板（`app_ui_sheet_*`）与共享 SVG 图标集（`assets/ui_*.svg`，经 `app_ui_image`/`app_ui_icon_set_color` 及行/动作/分组行的 `image_id` 变体使用）。尺寸与间距常量（4 px 网格、圆角 12/20/全圆、触控高度）位于 `app_ui_theme.h`。
- `home_app/`：表盘式主页：顶部 Wi-Fi/蓝牙/电池状态条、96 px 数字时钟、日期与时间源质量点，以及 2 x 2 信息卡（天气、时钟计时进度环、录音实时状态、水平仪），点击卡片进入对应应用；应用目录经 HOME 键进入。
- `menu_app/`：三列应用图标网格（72 px 圆形图标底），按 `launcher_order` 展示图标与名称。
- `clock_app/`：时钟枢纽页顶部为秒级数字时钟（日期与时间来源），下方分组列表含倒计时、
秒表、专注三行（带进度环摘要），时长选择从倒计时页的「自定义」进入；子页共享 App-scope
选中时长与运行会话；专注支持周期模式。
- `recorder_app/`：双声道录音主页与文件页；删除在页内底部面板中二次确认；录制、播放、删除与 `.part` finalize 全部经由
`recorder_service` worker，页面只提交命令并轮询 generation 快照。
- `calendar_app/`：公历月历页，只读取 `time_service` 本地时间与质量；提供上月/下月/今天导航、7 列日期网格与选中日期信息（星期、ISO 周号、年内第几天）。今日用 accent tint、选中日用 accent 描边区分；跨日由自有 1 s 定时器跟随，网格按显示月份重建，页面单屏不滚动。
- `calculator_app/`：单页四则运算计算器，静态页面（无 timer/订阅）与固定 5 x 4 键区；表达式行与结果行各自单行右对齐，引擎为纯逻辑模块 `calculator_math`，支持小数点、±、百分号、退格，按运算顺序从左到右求值，除零与溢出进入错误态并由 `C` 或数字键复位。计算过程只存在于页面内存，重新进入应用从 0 开始。
- `level_app/`：IMU 气泡水平仪；校准偏移经 `chore_service` 短任务读写并跨挂载保留，
恢复出厂后重载为零。
- `diagnostics_app/`：硬件自检页，由设置页五次点击入口打开，`HIDDEN` 标志使其不出现在普通应用目录。
- `settings_app/`：单卡片分组列表（六行：显示与电源、Wi-Fi、蓝牙、连接向导、设备状态、关于与维护）；提供亮度、固定熄屏/待机延迟选项、电源详情、连接管理、时间状态、SD 存储状态、运行时固件描述及恢复出厂设置确认页。忘记网络与解绑在页内底部面板中二次确认；恢复请求只有在 reset journal 持久化成功后才进入不可重复点击的重启等待状态；保存失败时页面保留并允许重试。
- `setup_app/`：`HIDDEN` 应用，从设置根页的「连接向导」行进入；开启限时 BLE 绑定窗口，显示六位 Numeric Comparison 并在本机确认，同时管理已保存网络的断开、重连、自动连接和忘记操作；网络选择与密码输入在手机侧完成，设备只显示六位 Numeric Comparison 并本机确认。
- `weather_app/`：展示当前天气、预警、24 小时和 7 日预报，包含预警列表与详情页；只消费 `weather_service` 快照，不执行网络、JSON 或缓存 I/O。
- `tests/host/`：共用导航、天气图标映射表、恢复出厂确认页、Setup Wi-Fi adapter、apps 持久化，以及各 App 纯逻辑模块（`*_math`）的宿主测试及最小依赖 fake；测试目标清单以该目录 `CMakeLists.txt` 的 `add_apps_host_test` 为准。

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

普通 `lv_image` 按原尺寸绘制、在对象框内居中裁剪且不缩放，因此**图标的图形必须落在
消费者实际绘制的窗口内**：App 图标以 64 单位设计框绘制、按 72 px 导出，启动器图标底
给 72 px、任务切换器给 56 px，全部图形须落在设计框居中的 `x,y = 13..50`；主页信息卡
字形只出现在 40 px 窗口，故按 40 导出；主页时钟卡的小环字形只出现在 28 px，单独用 `APP_IMAGE_HOME_RING_CLOCK`
记录。`tests/resources/test_icon_fit.py` 按语义 ID 记录每个绘制窗口并校验墨迹框，
新增或改动图标必须让它保持通过。

共享 UI 图标集位于 `common/assets/`，由 `common/resource_manifest.cmake` 登记到
`APP_IMAGE_UI_*` 语义段，以中性 INK 终色入库，运行时经
`lv_obj_set_style_image_recolor` 着色；`app_ui_image()` 在图片缺失时回退 LVGL symbol。
Setup 页的 Wi-Fi 卡片、绑定/断开/忘记/取消/完成/稍后等行图标均已改用该集合。页头动作
按钮（`app_ui_page_set_action`）为 42 x 42 圆形且右下对齐，位于系统控制中心顶部下拉捕获带
（屏幕顶部 20 px）之下，因此页头内容不得占用该顶部区域。

下拉刷新由 `app_ui_pull_refresh_attach()` 提供：在页顶放置一条被页头/内容遮挡的提示条，
`content`（以及可点击的 root）上向下拖动时整页（页头 + 内容）随位移下移并逐步露出提示条，
达阈值（约 25 px / 56 px）松手触发页面刷新回调并保持「正在刷新…」，页面经
`app_ui_pull_refresh_set_refreshing(false)`（或 8 s 兜底）回弹。天气根页复用
`weather_service_request_refresh()`，设置 Wi-Fi 页复用扫描请求；拖动起点落在可点击行上时
交给行/滚动，不触发下拉。控制中心顶部 20 px 下拉仍归控制中心。

每个应用以普通 `const` Page definition 描述 typed ops 及私有内存大小，并在 App 私有 route 表中显式绑定 `page_id`、definition 和 route `user_data`。App descriptor（`APP_MANAGER_APP_EXPORT` 及其 `APP_MANAGER_APP_EXPORT_META` 变体）进入 `.app_manager_apps` 链接段；`apps` 组件使用 `WHOLE_ARCHIVE`，App Manager 的链接脚本负责保留和发现该段。新增应用时应在独立目录中实现生命周期 ops，并把源文件加入本层的 `app_sources.cmake`（固件组件、sim 与 `tests/integration` 共用这一份清单，不在各处重复声明），同时提供图标 manifest，不要使用递归 glob；`tests/docs/test_app_registration.py` 会校验每条源文件都在清单内、消费方没有各自复制清单。

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

`APPS_SANITIZER` 还支持 `address`（ASan/UBSan）和 `thread`（TSan）。当前宿主测试验证 RUN/BACK/OPEN_PAGE 请求、统一命令池准入失败、ID 值复制、completion 恰好一次、天气图标映射表覆盖全部主图与小图资源且无死资源、恢复出厂确认页的失败重试与成功防重复、level 校准 save/load 与恢复出厂清除，以及保存网络操作过滤、快照回调和取消清理；不替代 ESP32-S3 上的界面、Numeric Comparison 确认、BLE、无线和内存验证。

音频、存储和时钟 worker adapter 的故障恢复测试，以及产品页面的跨层生命周期测试位于主工程 `tests/integration/`，通过 `CROSS_LAYER_SANITIZER` 分别运行普通、ASan/UBSan 和 TSan 配置。

## 设计与修改边界

页面资源必须与 App Manager 生命周期对应，稳定后台页不得保留 LVGL 对象、timer、事件订阅或页面 worker；释放失败应上报并保留可重试状态。Wi-Fi 连接和系统 SNTP 可跨页面存在，分别由 `connectivity_manager` 和 `time_service` 所有。UI worker 只投递音频、文件和 RTC 命令并读取线程安全快照，不直接执行 PCM、文件 I/O 或 RTC I2C。遵循主工程 `doc/code-style.md`，不得修改 ESP-IDF、`managed_components/` 或中间件实现来规避本层问题。

## 许可证

本项目采用 [MIT License](LICENSE)。
