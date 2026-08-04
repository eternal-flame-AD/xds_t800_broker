# xds-t800-broker

*Also available in [中文](README.zh.md).*

Wireless broker for the XDS (Branta) T-800 bike power meter. It relays the
proprietary T-800 payload over both ANT+ and the standard Bluetooth Cycling Power
Measurement Service so that regular head units can read it.

## Features

- Low power consumption suitable for touring (<15 mW when USB is not enumerated).
- Power and power balance readout
- Meter battery readout (shows up as "right/1st" battery on ANT+, "external" over BLE)
- Onboard battery gauge when powered via batteries with configurable discharge curve (shows up as "left/2nd" on ANT+, "internal" over BLE)
- Cadence readout (ANT+ only)
- Offset compensation (calibration) support with offset readback (ANT+ only, tested on a Garmin Edge 1050)

## Requirements

### Physical Design

- Minimal: nRF52840 Dongle with an off-the-shelf unregulated
  3×AAA-to-USB adapter (battery wired directly to VBUS).
  Allows for ~2.5 weeks of operation (10 h/day) on Ni-MH rechargeables.
  You can also use alkaline AAA or AA Ni-MH batteries to bump this number to ~4 weeks.
  By default the dongle will auto power-off after 15 minutes of no BLE or USB activity.
  Press any button to the wake the dongle up.
- Robust: nRF52840 Dongle in a custom enclosure
  with a hardwired battery. Requires the regulator modification described in the
  [hardware guide](https://docs.nordicsemi.com/r/bundle/ug_nrf52840_dongle/page/ug/nrf52840_dongle/hw_power_ext_reg_source.html).

Power banks or OTG cables are not recommended: most will shut off under light load, and the battery gauge will not work.

### Development

- nRF Connect SDK (NCS) v3.2.4.
- ANT SDK module cloned under the NCS tree (`<NCS>/ant`). The SDK module must be compatible with the SDK per the release notes, otherwise you might experience radio silence!
- nRF52840 Dongle or any compatible nRF52 board with the necessary peripherals defined (such as 52840 DK).

## Configuration layers

Configuration is split into three layers so that per-deployment changes do not
touch the fixed feature list:

- `prj.conf` — Required subsystem and feature selections. You generally do not
  edit this.
- `Kconfig.defconfig` — Factory defaults for upstream Kconfig symbols such as the
  Bluetooth device name and Device Information Service strings.
- `local.conf` — Per-build local overrides. Most users can simply leave this empty.

Any file matching `*.local.conf` or `local.conf` is ignored by git, so you can
keep deployment-specific overrides out of version control.

## Building

```sh
touch local.conf # see local.conf.example for ideas

mkdir build
west build --build-dir build \
  --board nrf52840dongle/nrf52840 \
  --sysbuild -- \
  -DCONF_FILE="prj.conf" \
  -DEXTRA_CONF_FILE="local.conf" \
  -DDTC_OVERLAY_FILE=boards/nrf52840dongle_nrf52840.overlay
```

`local.conf.example` shows optional overrides for
specialized setups (for example, calibrating battery gauge,
enabling Bluetooth privacy or changing the
Device Information Service strings). They are not recommended defaults!

Bluetooth privacy is off by default. The typical use case is either no
BLE connection at all, or only field-test connections via the
bonded NUS shell. You can enable it in `local.conf` if you are using
ANT+ only or are comfortable with the pairing workflow.

## Flashing

The nRF52840 Dongle is usually programmed over USB DFU,
triggered by the side-mounted RESET button.

### Generate and flash a DFU package

```sh
nrfutil pkg generate \
  --hw-version 52 \
  --sd-req=0x00 \
  --application build/xds_t800_broker/zephyr/zephyr.hex \
  --application-version 1 \
  build/zephyr.zip

nrfutil device program --traits nordicDfu --firmware build/zephyr.zip
```

## LED meaning

- Red: broker connected to the T-800 sensor
- Green: not used, reserved for a second custom central profile
- Blue: flashes briefly for every valid power packet received

## Pairing mode

First, make sure your meter is woken up and not connected to anything else,
the green LED at the base of the crank should be flashing green.

The dongle enters pairing automatically in factory condition.
To connect the broker to a new T-800 sensor, hold the dongle button down for 3 seconds
until the blue light flashes briefly. 
This clears the stored sensor peer for this boot and lets the broker find and persist a new sensor in close proximity.

You can verify the connection via the LED on the meter turning solid green, and
the serial number showing on your ANT+ head unit matching the last four digits
shown on your XDS Ride App.

Note: you need to connect to the sensor first before pairing the broker to your head unit.
ANT+ will not be activated until a sensor is connected.

## Advanced Usage and Development

### Bluetooth UART shell

The broker exposes a Zephyr shell over the Nordic UART Service (NUS) for
field tests. Because the shell includes sensitive commands like `devmem`,
the phone must form a LESC bond with the broker before NUS is usable.

For the first bond, connect the broker to a USB/UART shell and watch the
log output for the 6-digit passkey. Then pair with **nRF Toolbox** or
**nRF Connect**:

1. Scan and connect to the device named `XDS_T800_Broker` (or your
   `CONFIG_BT_DEVICE_NAME`).
2. Confirm LE Secure Connections pairing and enter the passkey shown
   in the shell log.
3. Once bonded, the UART module is unlocked and the broker streams log
   entries back to the phone.

Bonds are stored in flash and restored by `settings_load()` on boot, so the
phone only needs to pair once.

Should you decide to conduct field tests, choose a safe location and
always maintain situational awareness on the road!

### Bond management

A Zephyr shell is available over the console UART (USB CDC ACM on the dongle)
and, after bonding, over Bluetooth NUS. The following Bluetooth-related commands
are provided by `src/shell_bt.c`:

- `bt` — List Bluetooth identities and stored bonds.
- `bt unpair` — Remove all bonds.
- `bt unpair <address>` — Remove the bond for a specific address, for example
  `bt unpair DE:AD:BE:EF:00:00`.

This might be helpful for removing no longer trusted devices or clearing
up bond slots for new devices.

### Central profile system

The broker is built around a `central_profile` abstraction so that the program
can be adapted to work simultaneously with even more sensors
without rewriting the Bluetooth scanning, connection, and discovery logic.

A profile is defined in `src/central_profile.h`:

```c
struct central_profile {
  const char *name;
  const char *device_name_prefix;

  /* Called when a service is found during discovery. */
  int (*on_discovery)(struct bt_gatt_dm *dm);

  /* Called after the whole connection is up. */
  void (*on_connected)(struct bt_conn *conn);

  /* Called when the central disconnects. */
  void (*on_disconnected)(struct bt_conn *conn, uint8_t reason);

  const struct bt_uuid *service_uuids[];
};
```

The T-800 implementation is in `src/central_t800.c` and exported as
`central_t800_profile` in `src/central_t800.h`. It is registered in
`src/main.c`.

The main loop scans for peripherals that advertise all of the UUIDs
listed in `service_uuids` and whose device name starts with
`device_name_prefix`. When a match is found, the broker connects, discovers the
listed services, and calls `on_discovery` for each service. Once all services
are discovered, `on_connected` is called. The peer address is persisted to
settings so the broker can reconnect quickly on subsequent boots.

## License

See `LICENSE`.
