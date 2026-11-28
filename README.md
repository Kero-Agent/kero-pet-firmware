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
  app_face.c        默认画面：会眨眼、会转眼珠的表情（face 命令）
  face_render.c     表情的绘制代码，不依赖硬件，电脑上也能编译
  app_image.c       img 命令：接收电脑发来的图片并显示
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
docs/        网页调试台（见下面"网页调试台"一节）
tools/
  face_preview.c    在电脑上把表情画成图片，不用烧录就能看效果
  publish_firmware.py  把编译好的固件发布到 docs/firmware/，供网页烧录
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
| `lcd bl <0..100>` | 背光；`lcd test` 彩条；`lcd fill <颜色>`；`lcd invert <0\|1>`；`lcd seq [编号]` 切换初始化序列 |
| `cam` | 传感器 ID；`cam shot [帧数]` 抓帧并统计帧率 |
| `preview [秒]` | 摄像头画面显示到屏幕 |
| `face` | 恢复默认的眨眼表情；`face off` 停止并清屏 |
| `img show` | 再显示上一张上传的图片（上传在网页调试台的"屏幕"面板里） |

## 屏幕测试：表情和图片

屏幕初始化成功后，默认显示一个会眨眼、会转眼珠的表情。`lcd test` / `lcd fill` / `lcd rect`、`preview` 和上传图片会暂停表情，敲 `face` 恢复。

上传自己的图片：打开网页调试台的调试页，在"屏幕"面板里点"选择图片…"（可缩放、拖动位置），再点"发送到屏幕"。图片会裁成 360×360，从上往下逐行出现。图片只存在内存里，重启后恢复为表情；同一面板里的"恢复表情"随时切回去。

屏幕有条纹、花屏或者背光亮但没画面，多半是初始化序列和这块屏不匹配。固件里带了三套 ST77916 的序列，`lcd seq` 列出，`lcd seq <编号>` 切换并重新初始化（网页调试台的"屏幕"面板里也有这个下拉框），不用重新烧录就能逐个试。选择不掉电保存；试出来哪个对，把 `components/display/display_panel_st77916.c` 里 `s_variant` 的初值改成它。卖家给了专用序列的话，在同一个文件里加一张表。

图像有条纹但 `lcd invert 1` 能让画面变化，说明命令能到面板、图像数据有问题。`lcd lines` 画六条色带（白、黑、D0 深蓝、D1 暗绿、D2 绿、D3 紫红），哪条不对就查哪根数据线；`lcd clk 1` 把 QSPI 时钟降到 1 MHz，排除飞线太长引起的干扰。

屏幕是开机后才插上的话，敲 `init display` 重新发送初始化序列（面板不能回读，所以没接屏幕时 `status` 也显示 ok）。

## 网页调试台（`docs/`）

一个不用安装的网页，分两页：

- **调试**：每个模块一块面板（舵机、电机、IMU、电池、声音、屏幕、摄像头、I2C），底部是串口控制台。
- **展示**：机器人模型，可以拖动关节、点预设动作、用摇杆开底盘、换表情、播放声音。

网页通过浏览器的 Web Serial 连到上面这套命令行，固件不用改。要求电脑版 Chrome 或 Edge；其他浏览器只能用"模拟"模式，页面里的机器人是假的，用来看界面。

- 在线地址：`https://kero-agent.github.io/kero-pet-firmware/`。由 GitHub Pages 直接发布 `main` 分支的 `docs/` 文件夹：仓库 Settings → Pages → Source 选 Deploy from a branch，分支 `main`，文件夹 `/docs`。`docs/.nojekyll` 让 GitHub 原样发布，不经过 Jekyll。
- 本地打开：`cd docs && python -m http.server 8000`，然后访问 `http://localhost:8000`。直接双击 `index.html` 不行。
- 自检：`node docs/tests/selftest.js`，不需要浏览器和硬件；改动 `docs/` 时 `.github/workflows/web-test.yml` 也会自动跑一遍。
- 连接真机前先关掉 `pio device monitor`，一个串口只能被一个程序占用。
- **在网页里烧录固件**：调试页左侧"系统"卡片里，"重启固件"下面有"固件更新"。"烧录最新固件"把网页自带的固件（`docs/firmware/`）写进机器人，不用装任何开发工具；"烧录本地 firmware.bin…"只替换应用部分，用来试自己刚编译的版本。烧完会自动重启并重新连接。连不上时按住 BOOT，按一下 RST，松开 BOOT，再点一次。
- **发布新固件到网页**：`pio run` 编译后运行 `python3 tools/publish_firmware.py`，它把三个 bin 和清单 `manifest.json` 复制到 `docs/firmware/`，提交并推送后网页上就是新版本。先提交源码再发布，清单里记录的是编译时的提交号。

代码结构：`js/picture.js` 图片转换和上传，`js/flash.js` 固件烧录（用 `vendor/esptool-js/` 里乐鑫的 esptool-js），`js/link.js` 命令队列和串口，`js/sim.js` 模拟机器人，`js/robot.js` 把命令包成函数并解析输出，`js/debug.js` / `js/showcase.js` 两个页面，`js/pet.js` 模型。固件命令的输出格式改了，要同步改 `robot.js` 里的解析函数和 `sim.js`。

舵机通道默认按 CH0 脖子 α、CH1 脖子 β、CH2 左肩、CH3 左肘、CH4 右肩、CH5 右肘，可以在调试页改；校准值存在浏览器里，连接时写进机器人。

网页里还做不了的：摄像头画面（固件只能报每帧的平均颜色）、让真机圆屏显示展示页里选的表情（真机上只有默认的眨眼表情和上传的图片）、无线连接。

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
