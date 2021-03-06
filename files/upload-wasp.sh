#!/bin/sh

. /lib/functions.sh
. /lib/functions/system.sh
. /lib/functions/caldata.sh

WASP=/opt/wasp
BOARD=$(board_name)
MODEL=generic

case "${BOARD}" in
	avm,fritz3390)
		echo "Working on AVM FRITZ!Box 3390"
		MODEL=3390
	;;
	avm,fritz3490)
		echo "Working on AVM FRITZ!Box 3490"
		MODEL=3490
		;;
	*)
		echo "Unknown board detected, aborting."
		exit 1
        ;;
esac

do_extract_eeprom_reverse() {
  local offset=$(($2))
  local count=$(($3))
  local mtd=$1
  local file=$4
  local reversed
  local caldata

  if [ ! -e "${file}" ]; then
    mkdir -p $(dirname "${file}")

    reversed=$(hexdump -v -s $offset -n $count -e '/1 "%02x "' $mtd)

    for byte in $reversed; do
      caldata="\x${byte}${caldata}"
    done

    printf "%b" "$caldata" > "${file}"
  fi
}

do_extract_eeprom() {
  local mtd=$1
  local offset=$(($2))
  local count=$(($3))
  local file=$4

  if [ ! -e "${file}" ]; then
    mkdir -p $(dirname "${file}")

    dd if=$mtd of="${file}" iflag=skip_bytes bs=$count skip=$offset count=1
  fi
}

extract_eeprom() {
  local mtd

  mtd=$(find_mtd_chardev "urlader")

  case "${BOARD}" in
	avm,fritz3390)
		do_extract_eeprom_reverse ${mtd} 0x1541 0x440 "${WASP}/files/lib/firmware/ath9k-eeprom-pci-0000:00:00.0.bin"
	;;
	avm,fritz3490)
		do_extract_eeprom_reverse ${mtd} 0x1541 0x440 "${WASP}/files/lib/firmware/ath9k-eeprom-ahb-18100000.wmac.bin"
		do_extract_eeprom ${mtd} 0x198A 0x844 "${WASP}/files/lib/firmware/ath10k/cal-pci-0000:00:00.0.bin"
	;;
  esac

}

check_config() {
  local lan_mac
  local wifi_mac
  local wifi_mac2
  local r1
  local r2
  local r3

  if [ ! -e "${WASP}/files/etc/config/network" ]; then

    r1=$(dd if=/dev/urandom bs=1 count=1 |hexdump -e '1/1 "%02x"')
    r2=$(dd if=/dev/urandom bs=2 count=1 |hexdump -e '2/1 "%02x"')
    r3=$(dd if=/dev/urandom bs=2 count=1 |hexdump -e '2/1 "%02x"')

    lan_mac=$(fritz_tffs -n macb -i $(find_mtd_part "tffs (1)"))

    mkdir -p "${WASP}/files/etc/config"
    
    cat <<EOF >> "${WASP}/files/etc/config/network"
config interface 'loopback'
	option ifname 'lo'
	option proto 'static'
	option ipaddr '127.0.0.1'
	option netmask '255.0.0.0'

config globals 'globals'
	option ula_prefix 'fd$r1:$r2:$r3::/48'

config interface 'lan'
	option type 'bridge'
	option ifname 'eth0'
	option proto 'static'
	option ipaddr '192.168.1.2'
	option netmask '255.255.255.0'
	option ip6assign '60'
	option macaddr '$lan_mac'
EOF
  fi

  if [ ! -e "${WASP}/files/etc/config/wireless" ] ; then
    mkdir -p "${WASP}/files/etc/config"

    case "${BOARD}" in
    avm,fritz3390)
      wifi_mac=$(fritz_tffs -n macwlan -i $(find_mtd_part "tffs (1)"))
      cat << EOF >> "${WASP}/files/etc/config/wireless"
config wifi-device 'radio0'
	option type 'mac80211'
	option channel '11'
	option hwmode '11g'
	option path 'pci0000:00/0000:00:00.0'
	option htmode 'HT20'
	option disabled '1'

config wifi-iface 'default_radio0'
	option device 'radio0'
	option network 'lan'
	option mode 'ap'
	option ssid 'OpenWrt'
	option encryption 'none'
	option macaddr '$wifi_mac'
EOF
    ;;
    avm,fritz3490)
      wifi_mac=$(fritz_tffs -n macwlan -i $(find_mtd_part "tffs (1)"))
      wifi_mac2=$(fritz_tffs -n macwlan2 -i $(find_mtd_part "tffs (1)"))

    ;;
  esac

  fi

  if [ ! -e "${WASP}/files/usr/bin/wasp_script" ]; then
    mkdir -p "${WASP}/files/usr/bin"

    cat << EOF >> "${WASP}/files/usr/bin/wasp_script"
#!/bin/sh

/etc/init.d/dnsmasq disable
/etc/init.d/odhcpd disable
EOF
    chmod +x "${WASP}/files/usr/bin/wasp_script"
  fi
}

build_config() {
  if [ -e "${WASP}/config.tar.gz" ] ; then
    rm "${WASP}/config.tar.gz"
  fi
  cd "${WASP}/files"
  find . -type f | xargs tar zcf "${WASP}/config.tar.gz"
}

reset_wasp() {
  echo 0 > /sys/class/gpio/fritz${MODEL}\:wasp\:reset/value
  sleep 1
  echo 1 > /sys/class/gpio/fritz${MODEL}\:wasp\:reset/value
  sleep 1
}

if [ ! -e "${WASP}/config.tar.gz" ]; then
  extract_eeprom
  check_config
  build_config
fi

if [ ! -e "${WASP}/ath_tgt_fw1.fw" ]; then
  echo "${WASP}/ath_tgt_fw1.fw not found. Please extract it from AVM firmware and place it in ${WASP}"
  exit 1
fi

if [ ! -e "${WASP}/openwrt-ath79-generic-avm_fritzbox-${MODEL}-wasp-initramfs-kernel.bin" ]; then
  echo "${WASP}/openwrt-ath79-generic-avm_fritzbox-${MODEL}-wasp-initramfs-kernel.bin not found. Please download it from the OpenWrt website and place it in ${WASP}"
  exit 1                                                                                                                            
fi   

reset_wasp
n=0
until [ $n -ge 5 ]; do
  wasp_uploader_stage1 -f "${WASP}/ath_tgt_fw1.fw" -i eth0 -m ${MODEL} && break
  n=$[$n+1]
done
if [ $n -ge 5 ]; then
  echo "Error uploading stage 1 firmware"
  exit 1
fi

n=0
until [ $n -ge 5 ]; do
  wasp_uploader_stage2 -f "${WASP}/openwrt-ath79-generic-avm_fritzbox-${MODEL}-wasp-initramfs-kernel.bin" -i eth0.1 -c "${WASP}/config.tar.gz" && break
  n=$[$n+1]
done
