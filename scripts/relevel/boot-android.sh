#!/usr/bin/env bash
set -Eeuo pipefail
[[ "$(uname -m)" == aarch64 ]]
SDK_ADB=$(command -v adb || true)
if [ -z "$SDK_ADB" ]; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq adb build-essential linux-libc-dev
fi
sudo modprobe binder_linux
sudo modprobe binderfs 2>/dev/null || true
sudo mkdir -p /dev/binderfs
mountpoint -q /dev/binderfs || sudo mount -t binder binder /dev/binderfs
cc -O2 -Wall -Wextra -Werror -x c - -o "$RUNNER_TEMP/binderfs-add" <<'C'
#include <fcntl.h>
#include <linux/android/binderfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(void) {
    int fd = open("/dev/binderfs/binder-control", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("binder-control"); return 1; }
    const char *names[] = {"binder", "hwbinder", "vndbinder"};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        struct binderfs_device device = {0};
        snprintf(device.name, sizeof(device.name), "%s", names[i]);
        if (ioctl(fd, BINDER_CTL_ADD, &device) < 0) { perror(names[i]); close(fd); return 1; }
        printf("%s: %u:%u\n", device.name, device.major, device.minor);
    }
    close(fd);
    return 0;
}
C
sudo "$RUNNER_TEMP/binderfs-add"
docker pull --platform linux/arm64 redroid/redroid:13.0.0_64only-latest
docker image inspect redroid/redroid:13.0.0_64only-latest --format '{{json .RepoDigests}}'
docker run -d --name relevel-android --privileged \
  --platform linux/arm64 \
  -p 127.0.0.1:5555:5555 \
  -v /dev/binderfs/binder:/dev/binder \
  -v /dev/binderfs/hwbinder:/dev/hwbinder \
  -v /dev/binderfs/vndbinder:/dev/vndbinder \
  redroid/redroid:13.0.0_64only-latest \
  androidboot.redroid_width=720 androidboot.redroid_height=1280 \
  androidboot.redroid_dpi=240 androidboot.redroid_gpu_mode=guest
for attempt in $(seq 1 90); do
  adb connect 127.0.0.1:5555 >/dev/null 2>&1 || true
  if [ "$(adb -s 127.0.0.1:5555 shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then
    adb -s 127.0.0.1:5555 shell getprop ro.product.cpu.abi
    adb -s 127.0.0.1:5555 shell getprop ro.build.version.sdk
    adb -s 127.0.0.1:5555 shell id
    adb -s 127.0.0.1:5555 shell getconf PAGE_SIZE
    echo 'Native ARM64 Android booted successfully'
    exit 0
  fi
  sleep 2
done
docker logs relevel-android || true
adb -s 127.0.0.1:5555 logcat -d -t 200 || true
exit 1
