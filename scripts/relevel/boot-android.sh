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
#include <errno.h>
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
        if (ioctl(fd, BINDER_CTL_ADD, &device) < 0) {
            if (errno == EEXIST) continue;
            perror(names[i]); close(fd); return 1;
        }
        printf("%s: %u:%u\n", device.name, device.major, device.minor);
    }
    close(fd);
    return 0;
}
C
sudo "$RUNNER_TEMP/binderfs-add"
sudo chmod 666 /dev/binderfs/binder /dev/binderfs/hwbinder /dev/binderfs/vndbinder
getconf PAGE_SIZE
ls -l /dev/binderfs
ANDROID_IMAGE=redroid/redroid@sha256:5a42a569ee1d7c71796c0385e906cbaa4c3e0a162a56d9f26b29bdb1befac13b
timeout 180s docker pull --platform linux/arm64 "$ANDROID_IMAGE"
docker image inspect "$ANDROID_IMAGE" --format '{{json .RepoDigests}}'
docker run -d --name relevel-android --privileged \
  --platform linux/arm64 \
  -p 127.0.0.1:5555:5555 \
  -v /dev/binderfs/binder:/dev/binder \
  -v /dev/binderfs/hwbinder:/dev/hwbinder \
  -v /dev/binderfs/vndbinder:/dev/vndbinder \
  "$ANDROID_IMAGE" \
  androidboot.redroid_width=720 androidboot.redroid_height=1280 \
  androidboot.redroid_dpi=240 androidboot.redroid_gpu_mode=guest
DEADLINE=$((SECONDS + 180))
while [ "$SECONDS" -lt "$DEADLINE" ]; do
  timeout 8s adb connect 127.0.0.1:5555 >/dev/null 2>&1 || true
  if [ "$(timeout 8s adb -s 127.0.0.1:5555 shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then
    timeout 10s adb -s 127.0.0.1:5555 root
    ROOT_READY=0
    for attempt in $(seq 1 20); do
      timeout 5s adb connect 127.0.0.1:5555 >/dev/null 2>&1 || true
      if [ "$(timeout 5s adb -s 127.0.0.1:5555 shell id -u 2>/dev/null | tr -d '\r')" = 0 ]; then ROOT_READY=1; break; fi
      sleep 1
    done
    test "$ROOT_READY" = 1
    adb -s 127.0.0.1:5555 shell getprop ro.product.cpu.abi
    adb -s 127.0.0.1:5555 shell getprop ro.build.version.sdk
    adb -s 127.0.0.1:5555 shell id
    adb -s 127.0.0.1:5555 shell getconf PAGE_SIZE
    echo 'Native ARM64 Android booted successfully'
    exit 0
  fi
  sleep 2
done
docker inspect relevel-android --format '{{json .State}}' || true
timeout 12s docker exec relevel-android /system/bin/sh -c 'getprop; ps -A; logcat -b all -d -t 180' || true
sudo dmesg | tail -80 || true
docker logs --tail 200 relevel-android || true
timeout 10s adb -s 127.0.0.1:5555 logcat -d -t 200 || true
exit 1
