# third_party/avbtool

AOSP `avbtool` 单文件快照，用于离线校验留存的 AVB 镜像（`avb_guard.sh avb-verify`）。

## 来源

- 上游仓库：`https://android.googlesource.com/platform/external/avb`
- 文件：`avbtool.py`（AOSP 仓库里 `avbtool` 是指向 `avbtool.py` 的符号链接；本目录按用户要求落名为 `avbtool`）
- 分支/修订：`refs/heads/main` @ `761178607206f4cb2af79ed9eec52d8cbd814adb`
- 抓取方式：`https://android.googlesource.com/platform/external/avb/+/refs/heads/main/avbtool.py?format=TEXT`（base64）
- 抓取日期：2026-10-04
- 文件 sha256（本地留存副本）：`e5a664a38db623da00f080219bc0ee60a640a9dc4a872803616fae4938ac749b`
- 字节数：207502

## 许可

AOSP `external/avb` 采用 MIT 许可，文件头保留了完整版权与许可声明（`Copyright 2016, The Android Open Source Project`）。
本目录只做原样留存，不修改上游内容；如需升级，替换整文件并更新上面的修订与 sha256。

## 用法

```sh
python3 tools/device-guard/third_party/avbtool/avbtool info_image --image <vbmeta.img>
python3 tools/device-guard/third_party/avbtool/avbtool verify_image --image <vbmeta.img>
```

`avbtool` 只依赖 Python 3 标准库；无第三方 Python 依赖。宿主无 `python3` 时 `avb-verify` 会明确报错。
