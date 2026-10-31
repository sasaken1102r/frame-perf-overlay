# 変更履歴

## 未リリース

### Readouts (finaea's PR)

- The **Compact** switch is on every card that has more to show (all but GPU & RAM). Frame, CPU, Power, Network and General start compact, which is exactly their 0.4.0 card, so an updated panel keeps its shape (the default layout is unchanged). With Compact off:
  - Frame: CPU and GPU frame-time graphs (the worst frame per read, average and worst in ms, the budget as a dotted line)
  - CPU: a bar per core (an offline core doesn't shift the other bars), and each cluster's clock and current maximum ("capped from" while it's below the hardware maximum)
  - Power: the vph voltage, every rail with W, V and its share of vph, and what no rail measures
  - Battery: the V × A line, mAh (now / full / design), time to full / empty, health and cycles, the charger's mode, the set current, and with a cable in the USB type, USB-PD offer, what arrives and the sag
  - Temp: DDR and every spot: battery, display, exhaust, heat sink, NPU, camera, video, and near the Wi-Fi chip, flash, nRF radio and 40-pin connector
  - Network: both links in one card (the dongle, then Wi-Fi), each with its signal, rates, link rate, band, channel, width and IP address, or Not connected
  - General: uptime, the app in front and this overlay's own cost (CPU, memory, ≈ W)
  - Battery and Temp keep 0.4.0's meaning of `compact`. **Visible change:** a Battery or Temp that was not compact in 0.4.0 now shows these extra rows too (Temp 154 → 198 px, Battery 142 → 222–322 px, depending on which rows show); compact ones and the default layout are unchanged
- New widgets **CPU items** and **RAM items**: the busiest processes by CPU (% of one core) and by memory, every 2 seconds, with the number of rows (1-50), One row per name, and Hide system. A process shows by its comm name, or by its program's file name when the comm is a cut-off start of it ("at-spi-bus-launcher") or the other way round ("vrlink"). Hide system's list is `hide_rules.json` next to the settings file (written once with a built-in list, re-read when edited; built-in rules added in a later version are appended at start, and a file that doesn't load shows a line on the card while the last good rules stay in use)
- New readings are read only while a widget that shows them is on the panel and someone is looking, like the rest: processes every 2 s, uptime every 1 s, this overlay's own cost every 2 s, the app in front every 5 s and at once when SteamVR changes app. The last three are read only while General has Compact off. After the panel shows again, the process lists and the HUD cost read again 0.25 s later
- The layout file takes `"compact"` on the new types (`false` is written only when off; a missing key keeps the 0.4.0 card), and `"max_items"`, `"sum_same_name"` and `"hide_system"` on the process lists
- New UI strings are in English in both languages for now (TODO(ja))
- Tests: the new readers (fake `/sys` and `/proc`), the cards with Compact off, the process lists and hide rules, and that 0.4.0 layouts keep their size

## 0.4.0 — 2026-10-24

### ウィジェットの並べ替え・レイアウトの編集・プロファイル（finaea さんの PR #4）

- 決まった形だったパネルを、ウィジェットを並べる形にした。ウィジェットは「フレーム」「電力」「温度」「CPU」「GPU・メモリ」「通信」「電池」「その他」で、どれも今までと同じ値を同じ見た目で出すカード。既定のレイアウトは 2 列で（左に「フレーム」「電力」「温度」「CPU」、右に「GPU・メモリ」「通信」「電池」「その他」）、「温度」と「電池」は小さく表示。前のパネルのように横長で低い形にした（512×460px → 1024×422px ほど）。`width_m` は 1 列の幅なので、同じ `width_m` だと文字の大きさは同じまま、横幅は前の 2 倍になる
- ダッシュボードのタブを 3 つのページにした: 「配置」（今までのカード）、「レイアウト」、「プロファイル」
- 「レイアウト」のページ: ウィジェットをドラッグして列の間で動かす・パレットに落として外す・パレットから足す、「電池」と「温度」を短い「小さく表示」のカードにする。8 列、1 列に 16 個まで。`width_m` は 1 列の幅になった。変更はすぐ保存する（レイアウトの版を比べてから書き換えるので、古いレイアウトに対してした変更は、新しいほうを上書きせずに断る）
- 「プロファイル」のページ: レイアウトを 8 個までプロファイルとして保存できる。［使う］［上書き保存］［名前を変える］（SteamVR のキーボード）［削除］［新しく作る］［既定に戻す］。プロファイルは `~/.config/frame-perf-overlay/profiles/` のファイル
- 出しているものだけを、誰かが見ている間だけ読むようにした: 値ごとに持ち主のウィジェットと読み取り元を 1 つずつ登録表に決め、読み取り元は、パネルのウィジェットが必要としていて、使っているヘッドセットでパネルが出ているとき（またはダッシュボードのタブを開いているとき）だけ読む。パネルを隠している間と、ヘッドセットを外している間は何も読まない。読む間隔は前と同じ（電力と無線は 2 秒おき、電池と iio の温度は 5 秒おき、ほかは更新ごと）
- パネルが隠れたあとにまた出すと、CPU 使用率と通信速度は 0.25 秒後にもう一度読む（前は 0.5〜2 秒「--」だった）
- 値が変わったウィジェットだけを描き直し、パネルの画素は cairo が描いたまま（アルファを掛けた BGRA）で OpenVR に渡すようにした（RGBA に直さない）。`FRAME_PERF_RGBA_UPLOAD=1` で RGBA に直す処理を戻せる
- 設定ファイルは今までのキーを全部残し、`layout` と `active_profile` を足した。前の設定ファイル（`layout` が無い）は、設定を全部そのまま読み、2 列の既定のレイアウトにする。読めないファイルは、次に保存する前に `config.json.bad` として残す。保存するときは、データがディスクに届くのを待ってからファイルを置き換えるようにした
- 設定ファイルの `layout` だけが読めないときも、`config.json.bad` として残してから既定のレイアウトで保存する
- ファイルから読んだレイアウトの、空の列と、重複できないウィジェットの 2 つ目は外す。知らないウィジェットしかないレイアウトは既定に戻す
- 設定ファイル・プロファイルの文字列の壊れた UTF-8 や対になっていないサロゲートは U+FFFD にする（cairo に渡すと、その画面が再起動まで描けなくなっていた）
- パネルの見た目の変わったところ: 「電力」のカードは、全チャンネルの合計（vph を 2 回数えていた）ではなく、vph の下のレールの合計（「各ch計」）を出す。ファン回転数は「温度」のカードに移した。GPU 負荷は、フレームの段の見出しと fps のグラフの後ろの塗りから、「GPU・メモリ」のカードに移した。熱でクロックが制限されているときの赤い枠は、パネル全体ではなく「温度」のカードに付く。時刻に日付も出す
- 追加された画面の文言に日本語を足した。値が取れないとき、日本語では「N/A: 」を付けず理由だけ出す（「センサーなし」など）。プロファイルの操作の結果も日本語で出す
- オプションを足した: `--dump-png` に `--layout FILE` と `--bench N`、`--dump-settings-png` に `--preview-page` と `--preview-select`、`--debug-dump`、`--dump-metrics`
- テストを足した: 登録表、スケジューラー、レイアウトとその編集、ウィジェットと並べて描く部分、レイアウトの編集の画面、プロファイル、システムの読み取り元（作り物の `/sys` と `/proc` で）、0.3.2 の設定ファイルの読み込み

## 0.3.2 — 2026-10-17

- GPU 使用率がいつも 100% になっていたのを直した。SteamOS 0.5.6（カーネル 6.18）の msm では、DRM の fdinfo の `drm-engine-gpu`（GPU が働いた時間）が壊れていて 1 秒で数か月ぶん増え、`drm-cycles-gpu`（働いたサイクル数）も重いゲームの分をほとんど数えない（Half-Life: Alyx で GPU が追いついていないのに 4% ほど）。本体全体の GPU 使用率はこのカーネルでは正しく出せないので、代わりに「GPU 負荷」として、SteamVR が測っている 1 フレームあたりの GPU 時間（`m_flTotalRenderGpuMs`）× アプリの fps を出すようにした。作者の Frame で、Alyx（72Hz、58.6fps、16.5ms）で 97% ほど、SteamVR のホームだけ（90Hz、3ms）で 27% ほど。数えるのは VR のアプリとコンポジタの分だけ（ほかのプログラムが使う GPU は入らない）。設定のキー（`gpu_warn_pct` / `gpu_crit_pct`）はそのまま
- fdinfo を読む仕組みを外したので、30 秒おきに /proc 全体を調べる処理（1 回最大 20ms）もなくなった。`--print` には GPU 負荷を出さない（SteamVR がないと出せないため）

## 0.3.1 — 2026-10-17

### 軽くした（cinnacappu さんの PR #2）

- リリースのビルドをサイズ優先（`-Os`）にし、使わない関数・データの削除（`--gc-sections`）と LTO を使う。実行ファイル（strip 後）は 463KB → 267KB。`-DFRAME_PERF_OPTIMIZE_SIZE=OFF` で速さ優先（398KB）。作者の Frame では、どちらも CPU 時間はほぼ同じ
- 描いた画像を RGBA に直す処理を 4 画素ずつまとめ、全部不透明・全部透明のところは表を引かない。作者の Frame で 1 回 0.46ms → 0.28ms
- 更新ごとの細かい無駄を減らした: 配列の使い回し、GPU 使用率の走査で読んだ値を読み直さない、固定先の変換は設定の反映時だけ作る、`--verbose` でないときは処理ごとの時間を測らない、パネルで使わない電力の各チャンネルをコピーしない
- 設定ファイルのフォントのパスに改行などの制御文字があると、壊れた JSON を書いていたのを直した
- テストを足した: 画素の変換、設定ファイルの読み書き、CPU・GPU 使用率と電力（作り物のファイルで）、固定先の変換。`./build/ui-helpers-test --benchmark` で前の変換と速さを比べられる

## 0.3.0 — 2026-09-28

### 手首への固定と時計（TaiKeid さんの PR #1）

- 性能パネルを左手・右手のコントローラーに腕時計のように付けられるようにした。設定パネルの「位置」のカードのいちばん上に「固定先」（頭 / 左手 / 右手）を足した。左右の手は、それぞれの位置と向きを覚えている。設定ファイルに `attachment`（`"head"` / `"left_wrist"` / `"right_wrist"`、既定 `"head"`）と `left_wrist` / `right_wrist`（`x` / `y` / `z` / `yaw` / `pitch` / `roll`）を足した。今までの設定ファイルはそのまま使える
- 位置と向きのボタンは、どこに固定していても同じカードの同じボタンで、選んでいる固定先のものを動かす（別の画面は作らない）。手のときは、微調整の十字と「近く / 遠く」がパネル自身の軸で 1cm ずつ動かす（「↑ 上」はパネルの上の辺の方、「右 →」は右の辺の方、「近く」は面の表 = 見ている人の方。±50cm まで）。コントローラーの軸で動かすと、付けた手の向きによってどちらへ動くのか分かりにくかった（ささけんが実機で確かめた）。設定ファイルにはコントローラーの座標のまま保存し、パネルの軸の 1 歩を回転行列でコントローラーの座標の差分に直して足す。左右のコントローラーは同じ軸（描画モデルの部品の向きで確かめた）なので、同じボタンは左右の手で同じ向きに動く。位置のボタンは「手首の標準の位置」（PR の既定値 `x` 0・`y` 0.05・`z` 0.08・`pitch` −90）になり、「いまの位置」は標準の位置からの距離（cm）を出す
- 手の向きの約束は頭のパネルとそろえた: パネル自身の軸で yaw → pitch → roll、「↑ 上向き」で面がパネルの上側へ、「右向き →」で右側へ、「左に回す」で面を見て反時計回り。コントローラーの上に寝かせた向きを `pitch` −90 として数え（既定）、そこから上下 ±90°（`pitch` −180〜0）まで。寝かせた向きのまわりで回すので、左右・上下・回転がそれぞれ別の軸で効く
- 手に付けたとき、パネルの面が目の方から外れるほど薄くして消す（「傾けると消す」、既定オン）。消えきる角度は「消える角度」で 35〜90° の間で 5° ずつ変えられ、その 30° 手前から薄くなり始める（既定 45〜75°）。設定ファイルの `wrist_fade`・`wrist_fade_end_deg`。薄くなり方は時定数 80ms でなめらかにする
- コントローラーか本体のトラッキングが外れたら、フェードを待たずにすぐ隠す。戻ったら 0 から薄く出す
- 性能パネルのいちばん下の行に時計（本体の時刻）を足した。設定パネルの「パネル」のカードの「時計」でオフ / 12h / 24h を選ぶ（既定 24h）。設定ファイルの `clock_format`（`0` / `12` / `24`）。時計を出すときはパネルが 1 行ぶん高くなり（512×460）、オフなら今までと同じ 512×434。テクスチャは 460 のまま作り、オフのときは `SetOverlayTextureBounds` で上の 434 だけを使う（切り替えで作り直さないので、ちらつかない。切り替えたらすぐ描き直す）。`--dump-png` もオフなら 434 の高さで書き出す
- OpenVR が透明度・表示・置き場所の変更を断ったときは、うまくいったことにせず、少しあとにやり直す（頭のときも）。透明度を断られたときは、古い透明度のままパネルを出さない
- 「既定に戻す」で、固定先・両手の位置と向き・傾けると消す・時計も既定に戻す

### 設定パネル

- 設定パネルを 1600×738 にした（前は 1600×690）。3 つのカードを同じ高さの 5 段にそろえ、「パネル」に時計の行、「位置」に固定先の行を足した。手のときは「向き」のいちばん下の段が「自分に向ける / 正面向き」から「傾けると消す」「消える角度」に変わる
- 3 択のセグメント切り替え（固定先・時計）を足した。2 択と同じ見た目（選択中はアクセントの塗り ＋ ✓）

### CPU

- 手に付けているときは、パネルの見え方を確かめる間隔を、薄くなっている途中（角度がフェードの範囲に入っている間も）は 1/30 秒、見えきっている・消えきっているときは 0.1 秒にした。ほかの用事で起きたついでに済ませ、設定パネルのイベントは 0.5 秒おきと設定パネルが見えている間だけ見る。コントローラーの番号（役割から引く）は 1 秒ごとに聞き直す。作者の Frame で、左手に付けて 0.8% ほど（頭と同じくらい）

### そのほか

- テストを足した（`ctest --test-dir build`）: 手首の位置と向きの計算、頭と手で向きの約束が同じこと、頭と手の回転が互いに影響しないこと、フェードと確かめる間隔、断られた操作のやり直し、時計、設定ファイルの読み書き、設定パネルのボタンの当たり判定と見た目のずれ。`scripts/package.sh` のリリースのビルドではテストを作らない（`-DBUILD_TESTING=OFF`）
- README の画像（性能パネル・警告の色・設定パネル、日英）を時計つきで撮り直した

## 0.2.0 — 2026-09-27

### 更新の仕組み（新しく足した）

- パネルから新しい版を確かめて入れられるようにした。設定パネルの見出しの右（見出しの行の中）の帯に、今の版をいつも出す（例:「最新版です（0.2.0）」。版は v なしの数字だけ）。［今すぐ確かめる］で、キャッシュを飛ばしてその場で GitHub に確かめる
- 新しい版があれば「新しい版 0.2.1 があります」と［今すぐ確かめる］［更新する］を出し、「0.2.1 に更新しますか？」の確認（［やめる］［更新する］）を 1 回はさんでからインストールする。進み具合（ダウンロード中・確認中・展開中・入れ替え中）、入れ終わった知らせ、失敗したときの理由と［もう一度］［閉じる］も同じ帯に出す。「入れました」は、そのプロセスが動いている間は出し続ける
- 帯の見た目は Steam Frame 用アプリ共通の見本に合わせた: 新しい版があるとき・確認のときはピンクの枠と太字の文言（新しい版はピンクの文字）、［更新する］はピンク塗りの強調ボタン。更新に失敗したときは赤い枠と太字の赤い文言。確かめられなかったとき（オフラインなど）は枠は普通のまま文字だけ赤。更新中は右に「ダウンロードして入れ替えます。途中でこの画面が閉じて開き直すことがあります」を小さく出す。文言は共通の strings.md に合わせた（「最新版です」「もう最新版です」など）
- 自動確認は起動時と、そのあとは 24 時間に 1 回まで GitHub に問い合わせる（前回の答えを使い回す。確かめられなかったときは 1 時間後）。設定に `update_check`（既定 `true`）を足した。`false` で止まるのは自動確認だけで、［今すぐ確かめる］［更新する］はそのまま使える
- インストールは、リリースに添付した `SHA256SUMS` で tar.gz を照らし合わせてから展開し、そのリリースの `install.sh` を前回と同じオプションで動かす。`SHA256SUMS` が無い・合わないリリースは入れない（無いときは「ここからは入れられない版です」と案内する）。失敗しても今の版のまま動き続ける。インストールは一時的な systemd のユーザーユニット `frame-perf-overlay-update` で動くので、パネルが再起動しても続く。ログは `~/.cache/frame-perf-overlay/update.log`
- Steam Frame 用アプリ共通の更新の仕組み（非公開の frame-updater リポジトリ、93a5f1b）を `vendor/frame-updater/` に取り込んだ。`frame-update.sh`（POSIX sh）と、それを裏で動かす C++ の部品（`update_check.{h,cpp}`）
- `install.sh` は `frame-update.sh` を `~/.local/share/frame-perf-overlay/` に置き、そのときのオプション（`--no-autostart` があれば）を `~/.config/frame-perf-overlay/install-args` に書き残す（次の自動更新が同じオプションで `install.sh` を呼べるように）。`--uninstall` は `frame-update.sh` も消す
- `scripts/package.sh` は最初に `vendor/frame-updater/` が手で書き換えられていないか確かめ、リリースの tar.gz に `frame-update.sh` を入れ、隣に `SHA256SUMS` を書き出す。公開に使う `gh release create` の行を最後に表示する（実行はしない）
- v0.1.0 には更新の仕組みが無いので、v0.1.0 から 0.2.0 へは一度だけ手で `./install.sh` する必要がある

### 向き（新しく足した）

- 性能パネルを回転できるようにした。設定ファイルの `rotation`（`yaw` / `pitch` / `roll`、度）で、左右の向き・上下の傾き・画面内の回転を決める。既定は全部 0（今までと同じ、顔の正面と平行）。範囲外の値は丸め（±180 / ±90 / ±180）、知らないキーはログで知らせる
- ダッシュボードの「Perf」タブに「向き」のカードを足した: 左右・上下の向きと、画面内の回転（「左に回す / 右に回す」。面を見て反時計回り / 時計回り）。「1° ずつ / 5° ずつ」で 1 回に動く量を切り替え、その刻みの目盛りにそろえる（刻みは保存せず、起動するたびに 1°）。今の向き（左右・上下・回転）を見出しの右に出す
- 「自分に向ける」: 位置はそのままで、パネルの面を頭へ向ける（roll は今の値のまま残す）。「正面向き」: 回転なし（roll も 0）
- 位置のボタン（左上・右下など）を押すと、その位置で自動的に自分に向ける。微調整（上下左右・近く / 遠く）では向きを変えない。「既定に戻す」で向きも既定に戻す
- `--verbose` のとき、向きを変えるたびに、SteamVR が計算したパネルの向き（光線の当たり）と、こちらで計算した向きをログに出す

### 設定パネル

- 設定パネルを 1600×690 にした（前は 1200×650）。横に長くして縦を詰めた: 見出しの行に更新の帯を入れ、「パネル」「位置」「向き」のカードを横に 3 つ並べた。1px あたりの大きさは前と同じ（ダッシュボードでの幅は 3.73m）
- 更新の帯の文言が長いとき（英語の失敗の理由など）は、小さくしすぎずに 2 行に分ける

### Wi-Fi の表示

- 家の Wi-Fi で Steam Link を使っている（直通回線に相手がいない）と、Wi-Fi につながっていても「未接続」と出ていたのを直した。直通回線に相手がいれば今までどおり「直通」、いなければ本体がつないでいる Wi-Fi の親機の電波・リンク速度・流量を「Wi-Fi」として出す。どちらにもつながっていないときだけ「未接続」
- インターフェースは名前ではなく nl80211 の種類（AP / 子機）で見分ける。Wi-Fi の電波は、親機から受ける signal（OS の表示と同じ値）を使う。MAC アドレスと SSID は今までどおり読まない
- `--print` にも「直通」「Wi-Fi」「未接続」を出す。`--fake-wifi home:-47` で見た目を確かめられる

### 修正

- ＋（プログラムを起動）から起動したとき、常駐中でも表示を切り替えずにロック待ちで居座ることがあったのを直した。Steam 自体が systemd のサービスなので、＋から起動した子にも `INVOCATION_ID` が引き継がれ、サービスとして起動したと勘違いしていた。自分の cgroup が `frame-perf-overlay.service` かどうかで見分けるようにした
- `install.sh` が、アプリが終わったあと（ロックファイルに残った PID のプロセスがもういないとき）に途中で止まっていたのを直した。サービスを止めてから動く `./install.sh --uninstall` は、いつもここで止まって何も消せていなかった。「アプリを終了」で止めたあとの `./install.sh` も同じだった

### そのほか

- `--dump-settings-png` に `--preview-update`（uptodate / checking / available / manual / confirm / installing / installed / checkfailed / installfailed）を足し、更新の帯の見た目を SteamVR なしで確認できるようにした
- README の画像（性能パネル・警告の色・設定パネル、日英）を今の見た目で撮り直した

## 0.1.0 — 2026-09-27

最初の公開版。

### 性能パネル

- SteamVR の中に、頭に追従する小さなパネルを出し続ける（既定は視界の左下）。
- フレーム: アプリの fps とリフレッシュレート、GPU 使用率（目安）、GPU・CPU の 1 フレームあたりの時間、再投影の割合、落ちたフレーム。SteamVR がアプリを半分などに抑えているときはバッジを出す。
- 電力（主電源レール）とファン回転数、温度（CPU・GPU・電池・画面付近・排気・放熱）。熱でクロックが制限されている間は赤いバッジと赤い枠。
- CPU 使用率とクロック、GPU クロック、メモリ、本体とコントローラーの電池。
- Steam Link の直通回線: 電波の強さ（Wi-Fi アイコンと dBm）、流れている量、リンク速度。
- 直近 30 秒のグラフ。しきい値を超えた値は黄・赤で表示（しきい値は設定で変えられる）。
- 日本語と英語。最初の言語は Steam の言語設定に合わせる（日本語なら日本語、それ以外は英語）。

### 設定・操作

- SteamVR ダッシュボードの「Perf」タブで、表示・位置・大きさ・透明度・言語・自動起動を変えられる。変更は `~/.config/frame-perf-overlay/config.json` に保存。
- ダッシュボードの＋（プログラムを起動）の一覧に出る。常駐中にもう一度起動すると、パネルの表示と非表示を切り替える。
- ダッシュボードの「Perf」アイコンの「閉じる」、または設定パネルの「アプリを終了」で終了する（次の SteamVR 起動までは起動し直さない）。
- `--version` オプション。

### 配布

- `install.sh`: sudo なしで `~/.local` に入れて、systemd のユーザーサービスで SteamVR と一緒に起動する。`--no-autostart`、`--uninstall`。
- `scripts/package.sh`: Steam Frame の上で `dist/frame-perf-overlay-<バージョン>.tar.gz` を作る。
- 実行ファイルのライブラリの探し場所（RUNPATH）を SteamVR の OpenVR のフォルダだけにした（末尾の空の要素でカレントディレクトリからも探していたのを直した）。

### 公開前の見直し

- README を英語（`README.md`）と日本語（`README.ja.md`）の利用者向けの説明に書き直した。開発中の調査メモ、Steam クライアント内部の識別子、作者の環境の情報は削除。
- ソースのコメントから、SteamOS・Steam の内部のファイル名などを削除。
- LICENSE は MIT の原文だけ。同梱している `openvr.h`（BSD-3-Clause）と、実行時に使うシステムのライブラリ・フォントは THIRD_PARTY_LICENSES.md に。LGPL-2.1 の本文は `licenses/LGPL-2.1.txt` に同梱。
- 商標表記を Valve のブランドガイドラインの定型文に。AI（Claude）を使って作り、作者の実機で動作を確かめたことを明記。
- ログや画面に MAC アドレス・SSID を出していないことを確認。
