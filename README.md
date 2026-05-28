Out-of-tree Rockchip RK915 Wi-Fi driver for mainline Linux
=============

Origins
-------------
This is a port of BSP driver obtained from device vendor.  
See [original patch](https://github.com/stolen/rk915/blob/main/docs/0001-rk915.patch) for reference.  

As I'm a developer at [Rocknix](https://rocknix.org/), I needed this chip to work with
mainline Linux (6.12.29 at the moment). So, I tried to port the driver.

The driver currently targets Linux 7.1. The kernel-side changes are implemented
via the standard MMC card-quirks framework (matched by the `rockchip,rk915`
compatible on the SDIO card node) instead of vendor hacks in the MMC core.

How to use
-------------
  1. patch your kernel with [quirks patch](docs/mainline-linux-7.1-rk915-quirks.patch)
  2. add [needed sections](docs/mainline-linux-dts-example.dtsi) to your device tree
     (note `compatible = "rockchip,rk915"`, `keep-power-in-suspend` and `non-removable`)
  3. build this source as an out-of-tree module `make V=1 -C $(kernel_path) M=${PKG_BUILD} ... CONFIG_RK915=m`
  4. copy the `firmware/rockchip` dir to `/lib/firmware/rockchip` (loaded via request_firmware)
  5. `insmod rk915.ko`

Status
-------------
The driver is not usable yet.  
Known issues:
  * Soon after association chip disconnects from network
  * Unloading the driver will likely stall your system
