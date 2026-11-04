#!/usr/bin/env bash
set -Eeuo pipefail
uname -a
printf 'Architecture: %s\n' "$(uname -m)"
if [ -c /dev/kvm ]; then printf 'KVM: present\n'; else printf 'KVM: absent\n'; fi
if [ -f "/boot/config-$(uname -r)" ]; then
  grep -E '^CONFIG_(ANDROID_BINDER|ASHMEM|MODULES|MODULE_SIG_FORCE|SECURITY_LOCKDOWN)' "/boot/config-$(uname -r)" || true
fi
sudo modprobe binder_linux devices=binder,hwbinder,vndbinder 2>/dev/null || true
if [ ! -e /dev/binder ]; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq "linux-modules-extra-$(uname -r)" "linux-headers-$(uname -r)" || true
  sudo modprobe binder_linux devices=binder,hwbinder,vndbinder 2>/dev/null || true
fi
printf 'Binder filesystem: '
grep binder /proc/filesystems || true
printf 'Binder modules: '
lsmod | grep binder || true
printf 'Installed kernel headers: '
test -d "/lib/modules/$(uname -r)/build" && printf 'present\n' || printf 'absent\n'
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true
if [ -e /dev/binder ]; then
  echo 'Native ARM64 Android container prerequisites available'
else
  echo 'Native ARM64 Android container requires a kernel module build'
fi
