# Supported Devices

> 中文版本：[SUPPORTED_DEVICES_ZH.md](SUPPORTED_DEVICES_ZH.md)

Rows marked **Shizuku recommended** ship a profile with `recommend_shizuku = 1`.
The app automatically turns on the home-screen **Run via Shizuku** switch for
them at every start; you can switch it off for the current session, and while it
is off the app stops asking for Shizuku until the next start.

Shizuku runs the exploit as the shell user, which has no seccomp filter, so the
W3 seccomp bypass stage is skipped. To use it:

1. Start Shizuku (for example over ADB) and keep it running.
2. Tap the status card at the top of the app and grant access when prompted.

Rows without the marker don't turn the switch on automatically, but **you can
enable it manually on any device**. Running via Shizuku skips the W3 seccomp
bypass there as well, so it saves time even where it isn't required.

| Kernel                                                 | Devices                                                          |
| ------------------------------------------------------ | ---------------------------------------------------------------- |
| `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`  | Xperia 1 V (any version with latest OS) — **Shizuku recommended**                        |
| `6.1.115-android14-11-ga2521ca27699-ab13294383`        | POCO X6 Pro                                                      |
| `6.1.118-android14-11-ga3b9c44908dd-ab13320413`        | Redmi Note 15 Pro+                                               |
| `6.1.118-android14-11-gca0ef6d17716-ab13624819`        | Xiaomi 14                                                        |
| `6.1.138-android14-11-g0c3d559bcd85-ab14529422`        | Xiaomi 14                                                        |
| `6.1.138-android14-11-g6ab8c9a86a33-ab14396278`        | POCO X6 Pro                                                      |
| `6.1.138-android14-11-g44bda9e8f6e9-ab13792638`        | POCO X6 Pro                                                      |
| `6.1.138-android14-11-g965475777129-mi`                 | REDMI K80 — **upstream-listed; awaiting branch device retest**   |
| `6.1.138-android14-11-g2ecae636cf9b-ab14676408`        | Lenovo Yoga Tab Plus (TB520FU)                                   |
| `6.1.145-android14-11-g09f1c0074ad7-ab14226177`        | Infinix Note 50s 5G, Infinix GT 30 (X6876)                       |
| `6.1.145-android14-11-g74d1702dab4d-ab14669069`        | vivo T4                                                          |
| `6.1.145-android14-11-geaa643a2c0ee-ab14763719`        | Motorola Razr 50 Ultra / Motorola Razr+ 2024                     |
| `6.1.157-android14-11-gbd23337e42e7-ab14791245`        | Google Pixel 9a (Tensor G4) — **upstream-listed; awaiting branch device retest** |
| `6.1.162-android14-11-gce140c0e5bf5-ab15450923`        | Zenfone 11 Ultra                                                 |
| `6.1.162-android14-11-g752d9c17787d-ab15574904`        | Google Pixel 9 Pro / 9 Pro Fold (Tensor G4) — **upstream-listed; awaiting branch device retest** |
| `6.1.162-android14-11-g5e8b0cffebd1-ab15202165`        | Google Pixel 9a (Tensor G4) — **upstream-listed; awaiting branch device retest** |
| `6.6.30-android15-8-g54dcbfbef792-ab12368803-4k`       | Red Magic Tablet 3 Pro                                           |
| `6.6.77-android15-8-g4a507830d890-ab13636293-4k`       | Xiaomi Civi 5 Pro, REDMI K90 / 4 Turbo, POCO F7                  |
| `6.6.77-android15-8-g63ce7556864c-ab13994517-4k`       | Xiaomi 15                                                        |
| `6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k`  | Xiaomi 15 Pro, REDMI K80 Pro / K80 Ultra                         |
| `6.6.89-android15-8-g096cdb6ecefc-ab14358676-4k`       | OPPO Pad 4 Pro                                                   |
| `6.6.89-android15-8-g0889fe95bb10-ab14402178-4k`       | POCO X8 Pro Max                                                  |
| `6.6.89-android15-8-g8e4be6b47e40-ab14134548-4k`       | POCO X8 Pro                                                      |
| `6.6.89-android15-8-g42db9ecb036b-ab14487600-4k`       | Honor Magic V5 (10.0.0.164)                                      |
| `6.6.89-android15-8-gb99b4586a3ee-ab13754593-4k`       | Honor Magic V5 (9.0.1.160)                                       |
| `6.6.89-android15-8-gf4dc45704e54-abogki446052083-4k`  | OnePlus 13                                                       |
| `6.6.92-android15-8-g3637f4904cf5-ab13944661-4k`       | Red Magic Tablet 3 Pro, Red Magic 10 Pro, Red Magic 11 Air       |
| `6.6.102-android15-8-gab8eb70a71b8-ab14350911-4k`      | Nothing Phone 3                                                  |
| `6.6.102-android15-8-gb01b41c2647c-ab15574720-4k`      | Xiaomi 17T                                                       |
| `6.6.102-android15-8-gfe76d1bc97fd-ab14689815-4k`      | Xiaomi 17T                                                       |
| `6.6.118-android15-8-g2e6b9c3812c5-ab15114928-4k`      | OPPO Find N5                                                     |
| `6.6.118-android15-8-g93e223c276e7-abogki500782043-4k` | OPPO Find X8 Ultra, OnePlus 13 / ACE 5 Pro                       |
| `6.6.118-android15-8-g608a629fedf7-ab15154340-4k`      | REDMI K90 Ultra                                                  |
| `6.6.118-android15-8-gbf8cd367de7a-ab15314822-4k`      | Motorola Razr 60 Ultra                                           |
| `6.6.118-android15-8-gc44b714366cc-abogki519650608-4k` | REDMI K80 Pro / Turbo 5 Max, POCO X8 Pro Max, Xiaomi Pad 7 Ultra |
| `6.6.118-android15-8-ge56cf6b09cca-ab15511674-4k`      | REDMI K90 Ultra, POCO F7                                         |
| `6.6.118-android15-8-ge58033dc8ea6-abogki498046332-4k` | OPPO Pad 5, OnePlus Pad 2, OPPO Find X8s                         |
| `6.6.118-android15-8-gebdfad32d749-ab15099304-4k`      | OPPO Find X8 / Find X8 Pro                                       |
| `6.6.118-android15-8-g21be90ecfb5e-ab15480137-4k`     | Honor Magic V5 (10.0.0.105) — **upstream-listed; awaiting branch device retest** |
| `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` | REDMI K90 Pro Max                                                |
| `6.12.23-android16-5-g75e9b1c7ae7c-abogki463945075-4k` | Xiaomi 17 / 17 Pro / 17 Pro Max / 17 Ultra                       |
| `6.12.23-android16-5-g82efd98459a2-ab14457512-4k`      | OPPO Find X9 / Find X9 Pro                                       |
| `6.12.23-android16-5-ga8f88ad96df3-ab13929693-4k`      | OnePlus 15                                                       |
| `6.12.23-android16-5-gb2a876903b49-ab14541642-4k`      | OnePlus 15                                                       |
| `6.12.23-android16-5-gf1bdb13583da-ab13761046-4k`      | Red Magic 11 Pro, Tablet 5 Pro                                   |
| `6.12.30-android16-5-g6e872b4863d6-ab13847919-4k`      | REDMI Note 15 4G, POCO M6 Pro 4G                                 |
| `6.12.38-android16-5-g1d46253471dd-ab15048002-4k`      | Motorola Razr Fold                                               |
| `6.12.38-android16-5-g3c4da6410bcb-ab13872285-4k`      | Xiaomi 13T                                                       |
| `6.12.38-android16-5-g665eafb62659-ab14778838-4k`     | NX809J / NX888J — **upstream-listed; awaiting branch device retest** |
| `6.12.38-android16-5-g74ad46052215-ab14494108-4k`      | Lenovo Legion Y700 Wuji — **upstream-listed; awaiting branch device retest** |
| `6.12.38-android16-5-g844001fb8721-ab14552068-4k`      | OnePlus 15T                                                      |
