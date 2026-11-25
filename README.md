# Kero 桌面机器人固件（主控板 ESP32-S3）

调试版固件：每个硬件模块独立初始化、独立带一组串口命令，可以一块一块地验证主控板和电源板。
某个模块没接或坏了（没插电源板、没接屏幕、没插摄像头）不影响其它模块启动。

状态：能编译通过，还没有在实际硬件上验证。

## 目录

```
main/
  app_main.c        启动入口：初始化各模块 → 启动调试命令行
  app_modules.c     模块表（顺序、依赖、status / init 命令）
  app_preview.c     preview 命令：摄像头画面显示到屏幕
  Kconfig.projbuild 配置菜单 "Kero robot"：模块开关和参数
components/
  board/     引脚表 board.h（所有引脚只在这里定义）、主 I2C 总线、i2c 命令
  pca9685/   电源板上的 PCA9685 驱动
  servo/     6 路舵机（PCA9685 通道 0–5）
  motor/     2 路电机（DRV8833：GPIO45/46 出 PWM，PCA9685 通道 6/7 出方向）
  battery/   电池电压（GPIO1，分压 1/2）
  imu/       LSM6DS3TR-C
  audio/     MAX98357A 功放 + ICS-43434 麦克风（共用一个 I2S 口）
  display/   360×360 QSPI 屏 + 背光
  cam/       GC2145 摄像头（接口前缀是 kcam_，避开摄像头库里已有的 cam_init）
  shell/     USB 调试命令行
```

每个模块的结构相同：

- `include/xxx.h`：对外接口，`xxx_init()`、`xxx_is_ready()` 加功能函数。
- `xxx.c`：实现，日志标签就是模块名。
- `xxx_cmd.c`：这个模块的调试命令，和功能代码分开。

## 编译和烧录

PlatformIO（已验证用 espressif32 平台 8.0.0，对应 ESP-IDF 6.1）：

```
pio run                 编译
pio run -t upload       烧录（USB 接电源板的 Type-C）
pio device monitor      打开命令行
pio run -t menuconfig   配置菜单 → Kero robot
```

也可以直接用 ESP-IDF（5.3 及以上）：`idf.py set-target esp32s3 && idf.py build flash monitor`。

第一次烧录如果找不到串口：按住 BOOT，按一下 RST，松开 BOOT，再烧。

## 调试命令

上电后在串口终端里看到 `kero>` 提示符，输入 `help` 列出全部命令。

| 命令 | 作用 |
|---|---|
| `status` | 各模块是否初始化成功 |
| `init <模块\|all>` | 重新初始化某个模块（比如后插上电源板） |
| `sys` | 芯片、运行时间、复位原因、内存 |
| `log <标签\|*> <级别>` | 调整某个模块的日志级别，例如 `log servo debug` |
| `reboot` | 重启 |
| `i2c scan` | 扫描主 I2C 总线；`i2c r/w` 读写寄存器 |
| `bat [次数] [间隔ms]` | 电池电压 |
| `pca` | PCA9685 各通道状态；`pca us <通道> <微秒>` 等直接操作 |
| `servo <编号\|all> <角度>` | 舵机转到角度；`servo us` 直接给脉宽；`servo off` 释放；`servo sweep <编号>` 来回扫一次 |
| `motor <a\|b\|both> <-100..100> [ms]` | 电机速度，可带运行时间；`motor stop` / `motor brake` |
| `imu [次数] [间隔ms]` | 加速度、角速度、温度；`imu cal` 陀螺零偏 |
| `spk tone <Hz> [ms]` | 喇叭发声；`spk scale` 音阶；`spk vol <0..100>` |
| `mic level` | 麦克风电平；`mic echo [ms]` 录一段再放出来 |
| `lcd bl <0..100>` | 背光；`lcd test` 彩条；`lcd fill <颜色>`；`lcd invert <0\|1>` |
| `cam` | 传感器 ID；`cam shot [帧数]` 抓帧并统计帧率 |
| `preview [秒]` | 摄像头画面显示到屏幕 |

## 建议的上电顺序

1. 只接主控板（USB 经电源板供电，电源开关断开）：`status`、`sys`、`i2c scan`，应看到 0x6A（IMU），接了电源板还有 0x40。
2. `imu 10`、`bat`（开关断开时电池读数接近 0 是正常的）。
3. `spk tone 1000 300`、`mic level`。
4. `lcd bl 50` 看背光是否受控；`lcd test` 看彩条。
5. `cam shot 10`；再 `preview`。
6. 打开电源开关：`bat`，然后 `servo 0 90`、`servo sweep 0`，一个一个试；`motor a 30 500`。

## 硬件相关的注意点

- **控制台只能走 USB**。GPIO43/44（原 UART0）在板上是背光和麦克风数据，`sdkconfig.defaults` 已经设好；改回 UART 会编译报错。
- **上电瞬间背光会闪**：芯片 ROM 会在 GPIO43 上打印启动信息，这在固件里关不掉。
- **GPIO45/46 是启动配置脚**，上电时必须为低，固件没有给它们开上拉。
- **电机正反转不对称**：每个电机只有一路 PWM，正转是"驱动/滑行"，反转是"驱动/刹车"，同样的速度值反转低速时更有力。见 `motor.h`。
- **屏幕**：复位脚和触摸复位都没接 GPIO，只能软件复位。触摸芯片还没写驱动，先用 `i2c scan` 找到它的地址。
- **屏幕初始化序列**：现在用的是 `esp_lcd_st77916` 组件自带的序列。屏不亮或颜色不对时，把卖家给的序列填进 `components/display/display_panel_st77916.c`。换另一块屏时新增一个 `display_panel_xxx.c` 即可。
- **摄像头**：PWDN 固定为低、RESET 只有上电复位，固件不能给它断电重启。GC2145 没有 JPEG，输出 QVGA RGB565。
- **PCA9685 的内部时钟有几个百分点的偏差**。舵机角度不准时用示波器量一下脉宽，然后 `pca osc <实际频率Hz>` 修正。

## 还没做的

- 触摸、LVGL 界面。
- WiFi / 蓝牙。
- 舵机和电机校准参数的掉电保存（现在重启后恢复默认值）。
