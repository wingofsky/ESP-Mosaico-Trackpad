# 第三方来源

ESP-IDF 固定为官方仓库的 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`：
https://github.com/espressif/esp-idf/commit/7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe

`components/` 中仅 `esp_lcd_touch_cst9220/` 随仓库保存，因其包含项目所需的驱动修改。
其他组件保留在本地，已从 Git 跟踪中移除并忽略。准确版本、注册表地址、可用的上游提交和本地快照摘要见
[dependencies.lock.json](dependencies.lock.json)。`python3 tools/verify_dependencies.py`
在依赖准备齐全后检查组件字节是否与清单一致，不作为源码推送或主机测试的前置条件。
注册表包与上游 Git 目录可能包含不同的辅助文件，完整目录摘要不保证跨分发方式相同。

| 组件 | 版本 | 许可证 |
| --- | --- | --- |
| esp_tinyusb | 2.3.0 | Apache-2.0 |
| tinyusb | 0.21.0~2 | MIT；子目录保留各自声明，包括 BSD-3-Clause |
| esp_lcd_co5300 | 2.2.0 | Apache-2.0 |
| esp_lcd_touch | 1.2.1 | Apache-2.0 |
| esp_lcd_touch_cst9220 | 0.1.1 | Apache-2.0 |
| cmake_utilities | 0.5.3 | Apache-2.0 |

本地组件保留源码版权、元数据及许可证文本（`LICENSE` 或 `license.txt`），本次忽略未裁剪或删除其文件。

`main/board.c` 的管脚、屏幕初始化和电源软启动逻辑改编自 Espressif
[esp-mosaico-bsp](https://github.com/espressif/esp-mosaico-bsp) 的
`cdf6a398cd196263c90a3bd39b89e16a2b1fedfd`，原版权为
2026 Espressif Systems (Shanghai) CO LTD，Apache-2.0；改编说明保留在文件开头。

本地 CST9220 调整：补充 CMake 显式依赖以离线编译；将 ACK 哨兵识别为“无新样本”
并保留触点状态，避免轮询时误报抬手。真正损坏的报告返回错误，应用会释放按键。

手势代码及测试样本为本项目实现。算法参考来源见 README，未复制 libinput、QMK
或 1€ Filter 的源码，未引入这些项目的运行依赖。
