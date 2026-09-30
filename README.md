rk915: Rockchip RK915 Wi-Fi driver for Linux
=============

[日本語](README_ja.md)

An out-of-tree mac80211 driver for the Rockchip RK915 SDIO Wi-Fi chip,
derived from the vendor BSP driver. It builds against Linux 6.19.

Supported hardware
-------------
| | |
|---|---|
| Bus | SDIO, IDs `0296:5347` and `0296:5348` |
| Band | 2.4 GHz |
| Interface modes | station, P2P client |
| Firmware | `rk915_fw.bin`, `rk915_patch.bin` |

Requirements
-------------
  * Linux 6.19
  * An SDIO host driven by `dw_mmc`
  * The MMC card quirks for the RK915 applied to the kernel:
    [docs/mainline-linux-6.19-rk915-quirks.patch](docs/mainline-linux-6.19-rk915-quirks.patch).
    [docs/mainline-linux-7.1-rk915-quirks.patch](docs/mainline-linux-7.1-rk915-quirks.patch)
    is the same for Linux 7.1.
  * `CONFIG_CFG80211`, `CONFIG_MAC80211`, `CONFIG_PWRSEQ_SIMPLE`
  * `CONFIG_RESET_GPIO` if the power sequence has a single `reset-gpios` line

Building
-------------
```
make -C /path/to/linux M=$PWD modules
```

Cross-compiling for arm64:

```
make -C /path/to/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$PWD modules
```

Installation
-------------
```
make -C /path/to/linux M=$PWD modules_install
depmod
install -m644 firmware/rockchip/rk915_fw.bin firmware/rockchip/rk915_patch.bin /lib/firmware/rockchip/
```

Set `INSTALL_MOD_PATH` to install into another root file system. The module
is loaded automatically when the SDIO card is detected.

Device tree
-------------
The SDIO host needs a power sequence, `keep-power-in-suspend`,
`non-removable`, and a `wifi@1` child with `compatible = "rockchip,rk915"` and
the host-wake interrupt. The driver takes received data on the host-wake
interrupt, so it must be the right pin. See
[docs/mainline-linux-dts-example.dtsi](docs/mainline-linux-dts-example.dtsi)
and the binding [docs/rockchip,rk915.yaml](docs/rockchip,rk915.yaml).

Module parameters
-------------
| Parameter | Default | Description |
|---|---|---|
| `macaddr` | unset | MAC address of wlan0, `xx:xx:xx:xx:xx:xx`. Takes precedence over `mac-address` and `local-mac-address` in the device tree. Without any of them the address is random on every load. |
| `debug_mask` | `0` | Debug message topics, a bitmask; see `inc/debug.h`. Writable at run time. |
| `patch_features` | `1` | Firmware feature bits: 1 phy hang reset, 2 filter probe requests in power save, 4 filter broadcast and multicast in power save, 8 null frames from the firmware in power save, which the firmware rejects. |
| `lpw_no_sleep` | `1` | Keep the LMAC awake. `0` needs a wake-up path. |

To keep one MAC address across boots, set it in `/etc/modprobe.d/rk915.conf`:

```
options rk915 macaddr=02:12:34:56:78:9a
```

The driver identifies its interfaces by this address, so do not change it
later with `ip link set wlan0 address`.

Debugging
-------------
Statistics and parameters are in debugfs, under
`/sys/kernel/debug/ieee80211/phy*/rk915/`: `params`, `phy_stats`,
`mac_stats`.

Known issues
-------------
  * Receiving data continuously, for example a transfer of tens of megabytes,
    triggers repeated firmware error recovery, and the transfer slows down or
    stops.
  * Only station mode has been tested. P2P client mode, suspend and resume,
    and unloading the driver are untested.

License
-------------
GPL-2.0; see [LICENSE](LICENSE). The source files are GPL-2.0-only or
GPL-2.0-or-later. The firmware binaries come from the vendor BSP and carry no
license statement.

Credits
-------------
  * The vendor BSP driver: [original patch](https://github.com/stolen/rk915/blob/main/docs/0001-rk915.patch)
  * The port to mainline Linux, by a [ROCKNIX](https://rocknix.org/) developer
  * [sunshineinabox/rk915](https://github.com/sunshineinabox/rk915)
  * The fixes from ROCKNIX PR 3252
