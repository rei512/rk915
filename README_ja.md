rk915: Linux 用 Rockchip RK915 Wi-Fi ドライバ
=============

[English](README.md)

Rockchip の SDIO 接続 Wi-Fi チップ RK915 のための、カーネルのツリー外でビルドする mac80211 ドライバです。ベンダーの BSP ドライバを元にしています。Linux 6.19 でビルドできます。

対応ハードウェア
-------------
| | |
|---|---|
| バス | SDIO。ID は `0296:5347` と `0296:5348` |
| 周波数帯 | 2.4 GHz |
| インターフェースのモード | station、P2P client |
| ファームウェア | `rk915_fw.bin`、`rk915_patch.bin` |

必要なもの
-------------
  * Linux 6.19
  * `dw_mmc` で動く SDIO ホスト
  * カーネルに当てた RK915 用の MMC card quirk:
    [docs/mainline-linux-6.19-rk915-quirks.patch](docs/mainline-linux-6.19-rk915-quirks.patch)。
    Linux 7.1 用は [docs/mainline-linux-7.1-rk915-quirks.patch](docs/mainline-linux-7.1-rk915-quirks.patch) です。
  * `CONFIG_CFG80211`、`CONFIG_MAC80211`、`CONFIG_PWRSEQ_SIMPLE`
  * 電源シーケンスの `reset-gpios` が 1 本だけの場合は `CONFIG_RESET_GPIO`

ビルド
-------------
```
make -C /path/to/linux M=$PWD modules
```

arm64 向けにクロスコンパイルする場合:

```
make -C /path/to/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$PWD modules
```

インストール
-------------
```
make -C /path/to/linux M=$PWD modules_install
depmod
install -m644 firmware/rockchip/rk915_fw.bin firmware/rockchip/rk915_patch.bin /lib/firmware/rockchip/
```

別のルートファイルシステムに入れる場合は `INSTALL_MOD_PATH` を指定します。SDIO カードが見つかると、モジュールは自動で読み込まれます。

デバイスツリー
-------------
SDIO ホストには、電源シーケンス、`keep-power-in-suspend`、`non-removable` と、`compatible = "rockchip,rk915"` と host-wake 割り込みを持つ子ノード `wifi@1` が必要です。ドライバは host-wake 割り込みで受信データを取り込むので、このピンは必ず正しく指定してください。記述例は [docs/mainline-linux-dts-example.dtsi](docs/mainline-linux-dts-example.dtsi)、バインディングは [docs/rockchip,rk915.yaml](docs/rockchip,rk915.yaml) にあります。

モジュールパラメータ
-------------
| パラメータ | 既定値 | 説明 |
|---|---|---|
| `macaddr` | なし | wlan0 の MAC アドレス。`xx:xx:xx:xx:xx:xx` の形式。デバイスツリーの `mac-address`・`local-mac-address` より優先されます。どれも無いと、読み込むたびに乱数で決まります。 |
| `debug_mask` | `0` | 出力するデバッグメッセージの種類。ビットの意味は `inc/debug.h` を参照。動作中に変更できます。 |
| `patch_features` | `1` | ファームウェアの機能ビット。1 は PHY が固まったときのリセット、2 は省電力中のプローブ要求の破棄、4 は省電力中のブロードキャスト・マルチキャストの破棄、8 は省電力中の null フレームをファームウェアに任せる設定で、ファームウェアが受け付けません。 |
| `lpw_no_sleep` | `1` | LMAC をスリープさせない。`0` にするには、起こす手段が別に必要です。 |

再起動しても同じ MAC アドレスにしたい場合は、`/etc/modprobe.d/rk915.conf` に書きます。

```
options rk915 macaddr=02:12:34:56:78:9a
```

ドライバはこのアドレスでインターフェースを見分けているので、後から `ip link set wlan0 address` で変えないでください。

デバッグ
-------------
統計とパラメータは debugfs の `/sys/kernel/debug/ieee80211/phy*/rk915/` にあります。`params`、`phy_stats`、`mac_stats` の 3 つです。

既知の問題
-------------
  * 数十 MB のファイル転送のように、データを受信し続けると、ファームウェアのエラー回復が何度も起き、転送が遅くなるか止まります。
  * 試したのは station モードだけです。P2P client モード、サスペンドと復帰、ドライバのアンロードは試していません。

ライセンス
-------------
GPL-2.0 です。[LICENSE](LICENSE) を参照してください。ソースファイルは GPL-2.0-only または GPL-2.0-or-later です。ファームウェアのバイナリはベンダーの BSP に含まれていたもので、ライセンスの表示はありません。

謝辞
-------------
  * ベンダーの BSP ドライバ: [元のパッチ](https://github.com/stolen/rk915/blob/main/docs/0001-rk915.patch)
  * mainline Linux への移植: [ROCKNIX](https://rocknix.org/) の開発者
  * [sunshineinabox/rk915](https://github.com/sunshineinabox/rk915)
  * ROCKNIX PR 3252 の修正
