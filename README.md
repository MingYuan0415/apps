**English** | [简体中文](README_ZH.md)

# MicroTech Built-in Apps

This repository provides the LVGL built-in apps that ship with the MicroTech
firmware image. Each mounted Page owns an independent Screen; apps manage
their UI and foreground session through the App Manager lifecycle, read state
or issue operations through middleware services, and never own hardware
drivers directly.

## Directory Structure and Apps

- `common/`: unified 368 x 448 page skeleton (64 px title bar, 36 px title),
  circular icon-base navigation/command rows, value rows and semantic status,
  retaining the async navigation API; grouped lists (`app_ui_group_*`, 1 px
  separators between rows), capsule buttons (`app_ui_button_set_kind`
  primary/secondary/danger), segmented control (`app_ui_chip_*`), status pill
  (`app_ui_add_pill`), in-page modal bottom sheet (`app_ui_sheet_*`) and the
  shared SVG icon set (`assets/ui_*.svg`, used through `app_ui_image` /
  `app_ui_icon_set_color` and the `image_id` variants of rows / actions /
  group rows). Size and spacing constants (4 px grid, 12/20/full radii, touch
  height) live in `app_ui_theme.h`.
- `home_app/`: watch-face home page: a top Wi-Fi/Bluetooth/battery status bar,
  a 96 px digital clock, the date and the time-source quality dots, and a
  2 x 2 information card grid (weather, clock timer progress ring, calculator,
  calendar); tapping a card enters the corresponding app; the app catalog is
  entered with the HOME key.
- `menu_app/`: three-column app icon grid (72 px circular icon base), showing
  the icon and name per `launcher_order`.
- `clock_app/`: the clock hub page has a second-resolution digital clock at
  the top (date and time source) and a grouped list below with the countdown,
  stopwatch and focus rows (with progress-ring summaries); duration selection
  is entered from the countdown page's "自定义" (custom); sub-pages share the
  App-scope selected duration and the running session; focus supports cycle
  mode.
- `recorder_app/`: three pages: dual-channel recording home, playback library
  and recording detail. The home page puts the "开始/停止" (start/stop) round
  button and "暂停/继续" (pause/resume) in the same fixed-height control band
  (the pause slot is always reserved, only the button itself appears or
  disappears); when recording ends it shows an "已保存 m:ss" (saved) receipt;
  when a command is rejected the status row gives the reason (insufficient
  free space, storage card or audio unavailable, read/write failure); the
  auto-stop receipt at the 30-minute limit is "已保存 m:ss · 达到上限" (limit
  reached); the service entering ERROR does not lock the round button, and the
  next tap retries; capacity and the 30-minute limit are merged into one line
  of metadata, and with no card it shows "存储卡未挂载" (storage card not
  mounted) rather than 0 MB. The home page reads the index count only; when
  the index is stale (the card was written or deleted outside the app) it
  actively requests a republish and never stays on the previous card's count.
  Each playback library row is "local recording time + duration + arrow";
  tapping it goes directly to the detail page (duration, size, recording time,
  playback progress, play/stop and delete); deletion is confirmed twice on the
  detail page and returns to the list on success. When the index generation or
  the content revision does not match, the list refuses the jump and the
  detail page hides play and delete and shows "内容已变化" (content has
  changed). Long-pressing any row in the list enters multi-select: the row-end
  arrow becomes a check mark, an action bar "取消 / 全选 / 删除 N" (cancel /
  select all / delete N) appears at the bottom, tapping or long-pressing a row
  toggles the selection, and clearing the selection exits; deletion is
  submitted one by one after a single confirmation, on full success the
  receipt is "已删除 N 个录音" (deleted N recordings), and any failure aborts
  and shows the reason. During multi-select the list is frozen to the rows
  present on entry; the service republishing the index after each delete does
  not refresh rows or lose the scroll position, and the batch is rebuilt once
  at the end.
  Recording, playback, deletion and `.part` finalize all go through the
  `recorder_service` worker, and the page only submits commands and polls
  generation snapshots; library rows no longer need "select first, then press
  the button".
- `calendar_app/`: Gregorian month calendar page, reading only `time_service`
  local time and quality; provides previous month / next month / today
  navigation, a 7-column date grid and selected-date information (weekday, ISO
  week number, day of year). Today is marked with an accent tint and the
  selected day with an accent outline; a day rollover is followed by its own
  1 s timer, the grid is rebuilt for the displayed month, and the page fits a
  single screen without scrolling.
- `calculator_app/`: single-page four-function calculator, a static page (no
  timer/subscription) with a fixed 5 x 4 keypad; the expression row and the
  result row are each single-line and right-aligned; the engine is the pure
  logic module `calculator_math`, supporting decimal point, ±, percent and
  backspace, evaluating left to right in operation order; divide-by-zero and
  overflow enter an error state and are reset by `C` or a digit key. The
  computation lives only in page memory; re-entering the app starts from 0.
- `level_app/`: IMU bubble level; the calibration offset is read and written
  through `chore_service` short tasks and retained across mounts; after a
  factory reset it is reloaded as zero.
- `files_app/`: paginated storage card browsing and details. File rows wrap on
  the full name and their height is determined by content; tapping a directory
  enters it, and the "当前目录详情" (current directory details) of a non-root
  directory supports explicitly deleting an empty directory. 16 entries per
  batch, and the previous batch is restored through a bounded re-enumeration;
  the directory, batch position and versioned selection are retained for the
  App lifetime and restored after returning from the detail page or remounting
  the screen. The page receives result copies into its own heap buffer, and
  selection and deletion bind the relative path, the mount generation and the
  content revision number; old rows cannot be selected during a read/page
  turn. Details show the full path, type, size and actual modification time;
  deletion is confirmed twice and is not recursive; busy, stale, non-empty
  directory, permission, path too long and media failure are hinted
  separately.
  A new list request immediately invalidates the displayed batch, and the old
  rows are cleared by the poll timer after the tap callback returns; submit
  rejection and async failure also do not keep the old directory contents.
  With no card the hint points to storage management to remount, and busy
  versus media failure are hinted separately. The device status page treats
  both READY and BUSY as mounted, and BUSY keeps the warning color.
- `diagnostics_app/`: hardware self-test page, opened from the five-tap entry
  on the settings page; the `HIDDEN` flag keeps it out of the normal app
  catalog.
- `settings_app/`: single-card grouped list (seven rows: "显示与电源" (display &
  power), "声音" (sound), Wi-Fi, "蓝牙" (Bluetooth), "连接向导" (connection
  wizard), "设备状态" (device status), "关于与维护" (about & maintenance)); the
  hub scrolls once the rows exceed one screen. Provides brightness, a speaker
  volume page, fixed screen-off/standby delay options, power details, connection
  management, time status, runtime firmware description, a separate storage
  page (mount status, total/free amount, current operation, remount, refresh
  capacity, safe unmount, twice-confirmed format and format recovery, with a
  hint and a refusal of destructive operations while occupied) and a factory
  reset confirmation page. The sound page mirrors the control center row: a drag
  applies the level through `audio_service_set_volume()` and stays off flash,
  the release commits it once through `audio_service_set_volume_persisted()`,
  and a board without an audio path greys the slider and shows "--" instead of
  faking a level. Forget network and unbind are confirmed twice in
  the in-page bottom sheet; a reset request enters the non-repeatable
  reboot-wait state only after the reset journal is persisted successfully;
  when saving fails the page stays and allows a retry.
- `setup_app/`: a `HIDDEN` app, entered from the "连接向导" (connection
  wizard) row on the settings root page; it opens a time-limited BLE binding
  window, shows a six-digit Numeric Comparison and confirms it locally, and
  also manages disconnect, reconnect, auto-connect and forget for saved
  networks; network selection and password entry happen on the phone side, and
  the device only shows the six-digit Numeric Comparison and confirms it
  locally.
- `weather_app/`: shows the current weather, alerts, 24-hour and 7-day
  forecasts, including an alert list and a detail page; it consumes only
  `weather_service` snapshots and performs no network, JSON or cache I/O.
- `flybirds_app/`: single-page tap-to-rise avoidance game with four states:
  ready, running, paused panel and result; `flybirds_game` is the pure logic
  rules, `flybirds_synth` synthesizes 8 bars of chiptune music and four sound
  effects on 16 kHz stereo, and `flybirds_audio` mixes on a single-writer task
  and then writes to `audio_service`; the audio task does not call LVGL, takes
  an exclusive PLAYBACK lease first, stays silent while externally occupied
  and retries every 2 s in the foreground, and does not contend with other
  writers for DMA; the high score and the music/sound-effect switches are
  stored in a versioned `flybirds_pref` Blob and cleared by factory reset.
  The writer's completion signal precedes synth/PCM reuse; a failed close or
  lease release keeps the handle, and later open/close retries. The page's
  PAUSE pauses ticks only after audio close succeeds, and a failed navigation
  can still continue the game; on reopen the invalidated render cache
  immediately refreshes the HUD. With no music and no active sound effect,
  phases advance in batches at sequencer boundaries and zero PCM is output;
  silence does not change the beat, the lease or the supply period; an invalid
  preference Blob size uses the defaults, and actual storage errors continue to
  be reported.
- `tests/host/`: host tests and minimal dependency fakes for shared
  navigation, the weather icon mapping table, the factory reset confirmation
  page, the Setup Wi-Fi adapter, apps persistence, and each app's pure logic
  modules (`*_math`); the authoritative test target list is
  `add_apps_host_test` in that directory's `CMakeLists.txt`.

The read-only resources of each App must live in its own `assets/` directory
and be declared one by one in that App's `resource_manifest.cmake`; recursive
globs are forbidden. Image resources use SVG as the only in-repo source (PNG
is not committed): at build time `cmake/mt_app_resources.cmake` uniformly runs
`tools/asset_pipeline/svg2png.py` (PyMuPDF) to rasterize them into RGBA PNG,
which are then converted through the pinned LVGLImage.py into RGB565A8 BIN and
packaged into the single `res` partition; the width/height recorded in the
manifest is the export size, the same SVG may register several size records,
and kind takes `SVG|PNG|FONT`. SVG must be written in final colors
(`currentColor` is not allowed) and must carry a `viewBox`. At runtime they are
queried by semantic ID in `app_image_ids.h`; the App descriptor's `icon_id`
points to the same resource table, and a missing resource falls back to an
LVGL symbol in the system UI; adding a resource requires updating the manifest,
the semantic ID and `tests/resources` together, and running
`idf.py reconfigure` after changing the manifest. Weather condition icons are
the `N-fill.svg` vendor sources of QWeather Icons (MIT), shipped with
`weather_app/assets/qweather-icons-LICENSE.txt`.

A plain `lv_image` draws at its original size, center-crops inside the object
box and does not scale, so **the icon's artwork must fall inside the window the
consumer actually draws**: App icons are drawn in a 64-unit design box and
exported at 72 px, the launcher icon base gives 72 px and the task switcher
56 px, and all artwork must fall inside `x,y = 13..50` centered in the design
box; the home information card glyphs only appear in a 40 px window, so they
are exported at 40; the small ring glyph of the home clock card only appears at
28 px and is recorded separately with `APP_IMAGE_HOME_RING_CLOCK`.
`tests/resources/test_icon_fit.py` records every drawing window by semantic ID
and validates the ink box; adding or changing an icon must keep it passing.

The shared UI icon set lives in `common/assets/`, is registered by
`common/resource_manifest.cmake` into the `APP_IMAGE_UI_*` semantic segment,
is committed with a neutral INK final color, and is recolored at runtime via
`lv_obj_set_style_image_recolor`; `app_ui_image()` falls back to an LVGL symbol
when the image is missing. The Setup page's Wi-Fi card and its bind /
disconnect / forget / cancel / done / later row icons have all switched to this
set. The page-header action button (`app_ui_page_set_action`) is a 42 x 42
circle, bottom-right aligned, and sits below the system control center's top
pull-down capture band (the top 20 px of the screen), so page-header content
must not occupy that top region.

Pull-to-refresh is provided by `app_ui_pull_refresh_attach()`: a hint bar
covered by the page header/content is placed at the page top; dragging down on
`content` (and on a clickable root) moves the whole page (header + content)
down with the displacement and progressively reveals the hint bar; releasing
past the threshold (about 25 px / 56 px) fires the page refresh callback and
keeps "正在刷新…" (refreshing) until the page springs back via
`app_ui_pull_refresh_set_refreshing(false)` (or the 8 s fallback). The weather
root page reuses `weather_service_request_refresh()`, and the settings Wi-Fi
page reuses the scan request; when the drag starts on a clickable row it is
handed to the row/scroll and no pull is triggered. The control center's top
20 px pull-down still belongs to the control center.

Each app describes its typed ops and private memory size with a plain `const`
Page definition, and explicitly binds `page_id`, the definition and the route
`user_data` in the App-private route table. App descriptors
(`APP_MANAGER_APP_EXPORT` and its `APP_MANAGER_APP_EXPORT_META` variant) go
into the `.app_manager_apps` link section; the `apps` component uses
`WHOLE_ARCHIVE`, and the App Manager linker script keeps and discovers that
section. When adding an app, implement the lifecycle ops in a separate
directory, add its source files to this layer's `app_sources.cmake` (the
firmware component, the sim and `tests/integration` share this one list and do
not declare it in several places), and provide an icon manifest; do not use
recursive globs. `tests/docs/test_app_registration.py` verifies that every
source file is in the list and that consumers do not each copy the list.

Pages declare only the lifecycle phases they actually need; there is no need to
write empty callbacks for unused phases. For example, a static page needs only
`.mount` and `.unmount`; add `.resume` and `.pause` when foreground refresh is
needed; add `.start` or `.stop` only when state must be retained across mount
cycles. New apps use typed ops; raw handlers exist only for compatibility with
old pages.

Page UI is created and destroyed only in `ONMOUNT/ONUNMOUNT`, and the root
object must be attached to the Page Screen. Timers, event subscriptions,
workers and service sessions start and stop only in `ONRESUME/ONPAUSE`; a
failed cleanup must keep the resource handle and allow a lifecycle retry.
Every page's private state is constrained to
`APP_MANAGER_PAGE_STATE_BYTES` by a static assertion. All product pages use
typed Page ops, and alert details receive a value-copied alert key through a
Typed Blob; new pages follow the same lifecycle boundary.

When a Weather page returns to the foreground it subscribes to
`WEATHER_SERVICE_MSG` and acquires the current immutable snapshot, and on pause
it unsubscribes first and then releases; events only trigger a re-fetch by
generation and do not copy the hourly, daily or alert arrays. Alert details
read the alert key from the START/NEWINTENT arguments of a versioned Typed
Blob and do not depend on a mutable global shared across pages. The detail
page's temporary formatting buffer lives on the UI worker task stack. Images
are looked up as mmap descriptors by semantic ID, a missing resource uses an
LVGL symbol, and file paths or mutable image payloads are never stored in page
state.

The recording app writes 16 kHz/16-bit/dual-channel WAV through
`recorder_service`. Recording, playback, deletion and the `.part` to `.wav`
finalize are all executed asynchronously by the service worker, and the page
only submits commands and reads generation snapshots; when the service is
unavailable or the finalize fails, the page stays visible and allows a retry.
The file page keeps the latest index published by the service in an
independent heap buffer (at most 32 entries), takes the row title from that
row's modification time, and on tap hands the complete row metadata to the
App-scope pending selection (`recorder_app_select`/`copy_selection`: a 192-byte
path exceeds the 88-byte Typed Blob payload), then opens the detail page
instead of re-querying by index. That selection is cleared when the App stops
or the deletion succeeds; MOUNT only copies it without consuming it: screen-off
does PAUSE + UNMOUNT and screen-on remounts the same Page, so the detail page
must still resolve the original recording and keep reflecting the playback
state.
After a storage or index generation change, the list refuses to open that row
and hints "列表已变化" (list has changed), and the detail page hides play and
delete and hints "内容已变化" (content has changed); even with the same path it
will not switch to a replacement file. The row list is rebuilt by a 250 ms poll
timer after the callback returns, and the row metadata copies are released on
unmount. The detail page's delete confirmation sheet keeps the complete target
captured when it opened; stop-then-delete during playback uses the same target.
After a successful delete it returns to the playback library; after delete
admission it shows "删除中" (deleting) and returns only on a non-pending
terminal state with the same operation_id and last_error equal to ESP_OK;
a failure shows the mapped error reason and does not pass admission success off
as a finished delete.
The level uses IMU acceleration to compute the pitch and roll angles, and
calibration reads and writes complete through `chore_service` short tasks,
whose result the page only consumes; the calibration is persisted by the Apps
storage helper, and `apps_factory_reset_persisted_state()` deletes it and
reloads a zero offset. The diagnostics page is opened from the five-tap entry
in settings and does not appear in the normal app catalog.

## ESP-IDF Integration

This repository depends on the App Manager and the MicroTech middleware. Add
the relevant component directories to the project root `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/app_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/middleware/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
)
```

Declare `PRIV_REQUIRES apps` in the firmware entry component so that the
built-in app archive participates in the final link. The project must also
provide `app_core`, `app_theme`, `event_bus`, `chore_service`,
`connectivity_manager`, `device_link_service`, `factory_reset_service`,
`time_service`, `timer_service`, `power_service`, `imu_service`,
`audio_service`, `sd_storage_service`, `recorder_service`, `weather_service`,
`onboarding_service`, `nv_storage`, `freertos`, `heap`, `fatfs`,
`esp_app_format`, `esp_hw_support`, `mt_log` and LVGL; the authoritative
dependency list is the root `CMakeLists.txt`. The components require ESP-IDF
5.1 or later, with external RAM task stack support enabled.

## Host Tests

```sh
cmake -S tests/host -B /tmp/mt-apps-host -G Ninja \
    -DAPPS_SANITIZER=none
cmake --build /tmp/mt-apps-host
ctest --test-dir /tmp/mt-apps-host --output-on-failure
```

`APPS_SANITIZER` also supports `address` (ASan/UBSan) and `thread` (TSan). The
current host tests verify RUN/BACK/OPEN_PAGE requests, unified command pool
admission failure, ID value copying, completion exactly once, the weather icon
mapping table covering all main and small image resources with no dead
resources, failure retry and duplicate-success prevention of the factory reset
confirmation page, level calibration save/load and factory reset clearing, the
flybirds physics/scoring/collision and the synth silence/mix boundaries, audio
stream ownership and stop failure retry, factory reset clearing of the flybirds
preference and of the two calibration keys, and saved network operation
filtering, snapshot callbacks and cancel cleanup; they do not replace
on-device UI, Numeric Comparison confirmation, BLE, wireless and memory
verification on the ESP32-S3.

Failure-recovery tests for the audio, storage and clock worker adapters, and
cross-layer lifecycle tests for product pages, live in the main project's
`tests/integration/` and run the normal, ASan/UBSan and TSan configurations
through `CROSS_LAYER_SANITIZER`.

## Design and Change Boundaries

Page resources must correspond to the App Manager lifecycle, and a stable
background page must not retain LVGL objects, timers, event subscriptions or
page workers; a failed release must be reported and leave a retryable state.
Wi-Fi connections and system SNTP may exist across pages and are owned by
`connectivity_manager` and `time_service` respectively. The UI worker only
posts audio, file and RTC commands and reads thread-safe snapshots; it never
executes PCM, file I/O or RTC I2C directly. Follow the main project's
`doc/code-style.md`; do not modify ESP-IDF, `managed_components/` or middleware
implementations to work around problems in this layer.

## License

This project is licensed under the [MIT License](LICENSE).
