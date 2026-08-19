# xds-t800-broker

*[English](README.md)*

喜德盛（Branta）T-800 自行车功率计的无线转发器。它把 T-800 的专有数据包同时转发到 ANT+ 和标准的蓝牙骑行功率测量服务（Cycling Power Measurement Service），让普通码表都能读取。

## 功能特性

- 低功耗，适合长途骑行（USB 未枚举时 <15 mW）
- 功率踏频与左右平衡读数
- ANT+ 传感器温度 (Tempe, 1 degC 精度)
- 功率计电池读数（ANT+ 上显示为“右侧踏板/电池1”，BLE 上显示为“外部电池”）
- 使用电池供电时，板载电量计支持可配置放电曲线（ANT+ 上显示为“左侧踏板/电池2”，BLE 上显示为“内部电池”）
- 零点补偿（校准）支持，并支持回读偏移量（已在 Garmin Edge 1050 上测试）

## 硬件与开发要求

### 硬件方案

- 简单方案：nRF52840 Dongle + 市面上常见的 3 节 AAA 转 USB 电池盒（电池直接接 VBUS，无需升压模块）。
  使用镍氢充电电池时，每天 10 小时大约可运行约 2-3 周；
  换成碱性 7 号 或 5 号镍氢电池可延长至约 4 周。
  默认 15 分钟没有蓝牙或 USB 活动会自动进入节电模式，按任意键唤醒。
- 进阶方案：nRF52840 Dongle 装入定制外壳，并焊接固定电池。需要参考 Nordic 的
  [硬件指南](https://docs.nordicsemi.com/r/bundle/ug_nrf52840_dongle/page/ug/nrf52840_dongle/hw_power_ext_reg_source.html)
  对稳压器进行修改。

不推荐使用移动电源或 OTG 线：大多数移动电源在轻负载下会自动断电，且电量计无法正常工作。

### 开发环境

- nRF Connect SDK (NCS) v3.2.4
- 已克隆到 NCS 目录下的 ANT SDK 模块（`<NCS>/ant`）。该模块版本必须与 NCS 版本匹配，否则可能出现无线电静默！
- nRF52840 Dongle 或任何具备必要外设的兼容 nRF52 开发板 （比如 nrf52840 DK）

## 配置层级

配置分为三层，便于在不改动固定功能的前提下，单独管理每次部署的个性化设置：

- `prj.conf` — 必需的子系统和功能选择。通常不需要手动修改。
- `Kconfig.defconfig` — 上游 Kconfig 符号的出厂默认值，例如蓝牙设备名称和设备信息服务字符串。
- `local.conf` — 每次构建时的本地覆盖。大多数用户可以直接留空。

所有匹配 `*.local.conf` 或 `local.conf` 的文件都会被 git 忽略，因此你可以把部署相关的覆盖项排除在版本控制之外。

## 编译

```sh
touch local.conf # 可以参考 local.conf.example 中的示例

mkdir build
west build --build-dir build \
  --board nrf52840dongle/nrf52840 \
  --sysbuild -- \
  -DCONF_FILE="prj.conf" \
  -DEXTRA_CONF_FILE="local.conf" \
  -DDTC_OVERLAY_FILE=boards/nrf52840dongle_nrf52840.overlay
```

`local.conf.example` 中提供了一些可选的覆盖示例，例如校准电量计、启用蓝牙隐私或修改设备信息服务字符串。这些**不是**推荐默认配置！

默认关闭蓝牙隐私。典型使用场景是：完全不使用 BLE，或者仅通过已绑定的设备进行现场测试。如果你明确使用 ANT+ 连接或熟悉配对流程，可以在 `local.conf` 中启用。由于很多码表对隐私支持不佳，日常连接码表时建议保持关闭。

## 烧录

nRF52840 Dongle 通常通过 USB DFU 烧录。

### 生成并烧录 DFU 升级包

```sh
nrfutil pkg generate \
  --hw-version 52 \
  --sd-req=0x00 \
  --application build/xds_t800_broker/zephyr/zephyr.hex \
  --application-version 1 \
  build/zephyr.zip

nrfutil device program --traits nordicDfu --firmware build/zephyr.zip
```

这里的 `--application-version 1` 只是示例。如果你之后要再次升级，通常需要递增版本号，否则引导程序可能会跳过本次升级。

## 指示灯含义

- 红色：转发器已连接到 T-800 传感器
- 绿色：当前未使用，预留用于第二个自定义 central 配置文件
- 蓝色：每收到一个有效的功率数据包时闪烁一次

## 配对模式

首先确保功率计已唤醒，且未连接其他设备——此时曲柄底部的绿色指示灯应呈绿色闪烁。

出厂系统自动进入配对模式。要连接新的 T-800 传感器，请长按 Dongle 按钮。保持按住 3 秒左右直到蓝色指示灯短暂闪烁。这会清除本次启动时读取的已有传感器配对信息，并让转发器在近距离内发现并保存新的传感器。

连接成功后，可以通过以下方式确认：功率计上的指示灯变为常亮绿色，并且 ANT+ 码表上显示的序列号后四位与 XDS Ride App 中显示的后四位一致。

注意：需先连接传感器，然后配对码表。传感器未连接时 ANT+ 不会激活。

## 高级用法

### 蓝牙 UART Shell

转发器通过 Nordic UART Service（NUS）暴露一个 Zephyr shell，方便现场测试。

由于 shell 包含 `devmem` 等敏感命令，手机必须先与转发器建立 LESC 绑定，才能使用 NUS shell。

第一次绑定需要通过 USB/UART shell 获取并显示 6 位配对码，然后在手机 App 中输入确认。

如果进行路边现场测试，请选择安全地点，并始终注意周围环境！

#### 使用 nRF Toolbox 或 nRF Connect 配对

1. 在手机上打开 **nRF Toolbox** 或 **nRF Connect**。
2. 扫描并连接名为 `XDS_T800_Broker` 的设备（或者你通过 `CONFIG_BT_DEVICE_NAME` 自定义的名称）。
3. 转发器会请求 LE 安全连接配对，并在日志或 shell 输出中显示 6 位配对码。
4. 在手机 App 中输入配对码并确认。
5. 绑定完成后，Nordic UART Service 解锁。你可以在 nRF Toolbox 中打开 **UART** 模块发送命令，转发器也会将日志回传到手机。

绑定信息保存在 Flash 中，并通过 `settings_load()` 在启动时恢复，因此手机只需配对一次。

### 绑定管理

Zephyr shell 可通过控制台 UART（Dongle 上为 USB CDC ACM）访问，绑定后也可通过蓝牙 NUS 访问。`src/shell_bt.c` 提供以下蓝牙相关命令：

- `bt` — 列出蓝牙身份和已保存的绑定。
- `bt unpair` — 删除所有绑定。
- `bt unpair <address>` — 删除指定地址的绑定，例如 `bt unpair DE:AD:BE:EF:00:00`。

这有助于移除不再信任的设备，或为新设备腾出绑定槽位。

### Central 配置文件系统

转发器基于 `central_profile` 抽象实现，这样无需重写蓝牙扫描、连接和发现逻辑，就能同时适配更多传感器。

`src/central_profile.h` 中定义的配置文件如下：

```c
struct central_profile {
  const char *name;
  const char *device_name_prefix;

  /* 发现服务时调用。 */
  int (*on_discovery)(struct bt_gatt_dm *dm);

  /* 整个连接建立完成后调用。 */
  void (*on_connected)(struct bt_conn *conn);

  /* central 断开连接时调用。 */
  void (*on_disconnected)(struct bt_conn *conn, uint8_t reason);

  const struct bt_uuid *service_uuids[];
};
```

T-800 的实现位于 `src/central_t800.c`，并通过 `src/central_t800.h` 导出 `central_t800_profile`。该配置文件在 `src/main.c` 中注册。

主循环会扫描广播中包含 `service_uuids` 所有 UUID 且设备名称以 `device_name_prefix` 开头的外设。匹配后，转发器连接、发现列出的服务，并对每个服务调用 `on_discovery`。所有服务发现完成后调用 `on_connected`。对端地址会持久化到 settings 中，以便后续启动时快速重连。

## 许可证

详见 `LICENSE` 文件。
