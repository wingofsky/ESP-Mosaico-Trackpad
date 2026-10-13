# Mosaico Trackpad

将 ESP-Mosaico 变成 macOS 可直接使用的 USB 有线触摸板。主机识别为标准 HID 鼠标，
无需 Wi-Fi、云服务或常驻软件。硬件资料见 [Espressif 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide.html)。

| 操作 | 行为 |
| --- | --- |
| 单指滑动 | 移动光标 |
| 单指轻点 / 连续轻点两次 | 左键 / 双击 |
| 原地按住约 0.42 秒后滑动 | 左键拖动，抬手释放 |
| 双指轻点 | 右键 |
| 双指上下 / 左右滑动 | 垂直 / 水平滚动 |
| 按住 AI 实体键并滑动 | 左键拖动 |

整个屏幕都是触摸区域，滚动方向遵循 macOS 的鼠标设置。
当前使用鼠标 HID，不提供原生三指、四指或捏合手势。
**已知问题：原地长按拖动可能误触。** 本次公开整理保留现有手势行为，尚未调整拖动策略。

## 验证状态

- 0.2.0：已在 ESP-Mosaico V1.2 真机上验证光标、点击、滚动和拖动；后续使用反馈存在上述长按误触。
- 0.2.1：公开整理和安全加固版本；验证记录见 [VALIDATION.md](VALIDATION.md)。尚未烧录到真机，不把旧版实测视为新版验收。
- 仅支持 ESP32-S31、16 MiB NOR 和本仓库指定的既有分区布局。其他板型及全新空白板未验证。

## 本地测试

需要 C 编译器和 Python 3.10 或更新版本；测试不会访问串口。

```sh
bash tools/test.sh
```

包含带 AddressSanitizer / UndefinedBehaviorSanitizer 的手势与串口解析测试、
模拟设备烧录保护测试（普通模式和 `python -O`）及合成手势对比；不需要本地第三方组件。

## 固定工具链与构建

使用 [ESP-IDF 固定提交](https://github.com/espressif/esp-idf/commit/7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe)。
这是一份支持 ESP32-S31 的开发版 SDK；第三方组件的来源、版本和本地快照摘要见
[dependencies.lock.json](dependencies.lock.json) 与 [THIRD_PARTY.md](THIRD_PARTY.md)。

Git 仅保留含项目修改的 `components/esp_lcd_touch_cst9220/`。
其余五个第三方组件为本地依赖，已通过 `.gitignore` 排除，文件不随 Git 克隆。
需要构建时，先按清单在 `components/` 下准备对应版本，再执行组件摘要校验。

先安装 Git、Python 3.10+、CMake、Ninja 及系统 C 编译器，然后执行一次：

```sh
mkdir -p "$HOME/.cache/mosaico-trackpad"
git clone https://github.com/espressif/esp-idf.git "$HOME/.cache/mosaico-trackpad/esp-idf"
git -C "$HOME/.cache/mosaico-trackpad/esp-idf" checkout --detach 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe
git -C "$HOME/.cache/mosaico-trackpad/esp-idf" submodule update --init --recursive
"$HOME/.cache/mosaico-trackpad/esp-idf/install.sh" esp32s31
```

在本仓库的 Bash / Zsh 中构建：

```sh
source tools/env.sh
python -m pip install -r requirements-tools.txt
python tools/verify_dependencies.py
idf.py build
```

已有同一版本的 SDK 时，可设置 `TRACKPAD_IDF_PATH`；自定义工具安装位置可设置
`IDF_TOOLS_PATH`。`tools/env.sh` 会拒绝错误提交或有已跟踪修改的 SDK。构建不会烧录。
烧录工具固定使用 esptool 5.4.0，避免工具 API 变更使保护失效。

## 备份与应用更新

此流程保留设备已有的 bootloader、分区表、Recovery、NVS 和资源分区，
仅更新 `ota_0`（`0x210000`）。不会初始化空白设备，也不会更改启动分区选择。
**`idf.py flash`、擦除及通用合并镜像操作已拦截**，避免覆盖 Recovery；直接绕过脚本调用
esptool 不受此保护。断电等硬件故障仍可能需要用完整备份恢复。

1. 手动让目标板进入 ROM 下载模式（按住 BOOT，再按 POWER 开机），确认串口名称。
2. 指定该串口，创建当前设备的完整备份；以下尖括号内容需替换：

   ```sh
   python tools/flash_app.py backup --port '<ROM串口>' --device private/device.json
   ```

3. 构建成功后，使用同一备份执行应用更新：

   ```sh
   bash tools/flash_app.sh --port '<ROM串口>' --device private/device.json
   ```

备份包含 16 MiB 闪存和设备身份，文件权限为 `0600`，仅存入 Git 忽略的 `private/`。
脚本不覆盖既有备份；再次备份时换一个文件名。每次写入前检查芯片、设备身份、闪存容量、
安全状态、固定分区布局、备份完整性和所有非应用区域。写入后独立校验应用及所有保留区域，
全部通过才重启。任何校验失败都应先处理原因，不要通过禁用校验继续写入。

## USB 诊断

默认固件仅支持 `status`，可查看外设状态及计数：

```sh
python tools/serial_command.py status --port '<运行固件的串口>'
```

`idf.py menuconfig` → `Mosaico Trackpad` 可显式开启开发诊断。
开启后，串口客户端能发送 `trace on/off`、`hidtest` 和 `bootloader`，分别输出触点、
发送一组小幅反向鼠标移动、重启进入 ROM 下载模式；仅用于可信主机上的开发。
默认构建关闭这些命令。解析器每轮最多处理 128 字节，超长及含控制字符的命令整行丢弃。

`tools/hid_probe.c` 是 macOS IOHID 只读报告观察工具，受系统输入监控权限控制。
调试工具可能生成设备身份、光标坐标或触摸轨迹，日志应放在忽略的 `artifacts/` 中。

## 手势算法

0.2.0 的实现参考以下公开资料，没有复制这些项目的源码：

- [libinput 滞回](https://wayland.freedesktop.org/libinput/doc/latest/touchpad-jitter.html)：2 像素移动死区抑制抖动。
- [libinput 滚动判定](https://wayland.freedesktop.org/libinput/doc/latest/scrolling.html)：主方向启动，过滤小幅横漂。
- [QMK Cirque 驱动](https://github.com/qmk/qmk_firmware/blob/master/drivers/sensors/cirque_pinnacle.c)：整数余数累计保留慢速滚动。

另有 24 毫秒抬手确认、逐触点轻点验证及异常输入释放保护。
`python3 tools/compare_gestures.py` 使用保留的 0.1.0 源码样本和合成输入比较；不依赖旧 Git 历史或私人触摸记录。

## 公开与许可证

原创代码采用 [Apache-2.0](LICENSE)，第三方代码保留各自许可证及版权声明，见 [NOTICE](NOTICE)。
推送前运行 `python3 tools/check_publication.py` 检查全部可达 Git 历史。
请通过 Git 推送源码，勿上传整个工作目录；本地第三方依赖、`private/`、`artifacts/`、构建产物及旧私有历史不属于公开内容。
