# Y8960 Sequencer Player

[Y8960 BASIC Extension](https://github.com/madscient/Y8960BasicExtension) が
`CALL MSAVE` で書き出すシーケンスデータ（Y8SQ 形式）を、音源チップのエミュレータで
鳴らすスタンドアローンのプレイヤー。Windows / Linux / macOS 向け。

**開発中。** シーケンスデータを読んで鳴らせる。

鳴らせる音源は、Y8960 の8つの音源ブロック（SSGS、OPLLEX ×2、OPL2EX ×2、DCSG ×2、
SCC）と、Y8SQ 形式が表す OPL3・OPM・OPNA・OPNB。OPNB は YM2610B（FM 6 チャンネル）
として鳴らす。

## 使い方

コマンドライン。鳴らし終えると終わる。

```
y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>] [--tick <0-2>]
            [--repeat <0-255>] [--mute <指定>]... [--wav <出力ファイル>]
```

`--tick` は演奏を進める割り込みの周期で、値の意味は `CALL MINIT` の分解能と
同じ。0 が約60Hz、1 が約100Hz、2 が約200Hz、省略時は 2。**曲の速さは変わらない。**

`--repeat` は曲を鳴らす回数で、値の意味は `CALL MSTART` のリピート回数と同じ。
0 は終わらない（Ctrl+C で止める）。省略時は 1。

`--wav` を付けると、鳴らす代わりに WAV ファイルへ書き出す。48000Hz・16bit・
ステレオの PCM。曲が終わったあとも、音が消えるまで（最長 5 秒）書く。
**`--repeat 0` とは一緒に使えない。** 念のため、30 分を超えたら打ち切る。

`--mute` は黙らせるものを指定する。繰り返して指定できる。

| 指定 | 黙るもの |
|---|---|
| `<チップ>` | そのチップのトラック全部。チップは `SSGS` `OPLLEX1` `OPLLEX2` `OPL2EX1` `OPL2EX2` `DCSG1` `DCSG2` `SCC` `OPL3` `OPM` `OPNA` `OPNB` |
| `<チップ>,<CH番号>` | そのチャンネル。番号はシーケンスデータと同じ（下の表） |
| `T<番号>` | トラック 0-15 |
| `A`-`P` | トラック 0-15 を英字で（`A` がトラック 0） |

- 頭に `!` を付けると、指定したもの**以外**を黙らせる。`!` の付いた指定を複数
  書くと、そのどれにも当たらないものが黙る。`!` の無い指定はそのうえで黙らせる
- シーケンスが使っていないチップ・チャンネル・トラックを指定しても、何もしない
- 大文字と小文字は区別しない

| チップ | CH番号 |
|---|---|
| `SSGS` | 0-5 |
| `OPLLEX1` `OPLLEX2` | 0-8、10 がリズム |
| `OPL2EX1` `OPL2EX2` | 0-8、9 が ADPCM、10 がリズム |
| `DCSG1` `DCSG2` | 0-2 が矩形波、3 がノイズ |
| `SCC` | 0-4 |
| `OPL3` | 0-17 が 2OP、18-23 が 4OP、24 がリズム |
| `OPM` | 0-7 |
| `OPNA` `OPNB` | 0-5 が FM、6-8 が SSG、9 がリズム（OPNB は ADPCM-A）、10 が ADPCM-B |

GUI。引数は省略できる。操作は Windows Media Player に倣っている。画面の文字は英語。

```
y8960gui [シーケンスファイルかフォルダ] [--adpcm <ADPCM サンプルファイル>]
```

- **プレイリスト**：右のペインに出る。`Hide playlist` / `Show playlist` で隠せる
  - ファイルやフォルダをウィンドウに落とすと、プレイリストを作り直してすぐに鳴らす。
    プレイリストのペインの上に落とすと後ろに足す（止まっていればすぐに鳴らす）
  - フォルダはその直下の `.sq` ファイル（大文字と小文字を問わない）を名前の順に並べる。
    名前の中の数字は数として比べる（`2` が `10` より前）。サブフォルダは見ない
  - `Y8PC` 形式のファイルを落とすと、ADPCM サンプルファイルとして読む
  - `Open...` と `Open folder...` はプレイリストを作り直して鳴らす。ペインの
    `Add files...` と `Add folder...` は後ろに足すだけで、鳴らさない
  - ダブルクリックか Enter でその曲を鳴らす。Delete で消す。行を引きずって並べ替える。
    右クリックのメニューにも「鳴らす」と「消す」がある。読めない曲は灰色になり、飛ばす
- **曲ごとの繰り返し（`Loops`）**：曲をその回数だけ繰り返したら、5 秒でフェードアウトして
  次の曲へ進む。0 にすると曲は終わらず、次の曲へは進まない。鳴っている途中で変えても効く
- **プレイリストのリピート（`Repeat`）**：最後の曲のあとで頭に戻る。`Shuffle` で順を混ぜる
- 前（`|<`）・再生と一時停止・次（`>|`）・`Stop`。キーボードでは Ctrl+P（再生と一時停止）、
  Ctrl+S（止める）、Ctrl+B（前）、Ctrl+F（次）、Ctrl+H（シャッフル）、Ctrl+T（リピート）、
  Ctrl+O（開く）
- 音源ブロックごと・チャンネルごとのレベルメーターが出る。リズムチャンネルは楽器ごとに
  分かれる（OPNA・OPNB は6つ、ほかは5つ）。帯の `Mute` でチップを、バーのクリックで
  チャンネルを黙らせる（リズムの楽器はリズムチャンネルの1本として黙る）。帯の左のレバーでチップごとの音量を -40 dB（一番下は
  無音）から +6 dB まで変えられ、右クリックで 0 dB に戻る
- tick の周期は止まっているときに選べる
- 引数で渡したファイルやフォルダはプレイリストに入れるだけで、鳴らさない

- シーケンスファイルは名前を問わない。中身の `Y8SQ` ヘッダで判断し、MSX の
  `BSAVE` で保存した見出し付きのファイルも読める
- ADPCM サンプルファイルは `Y8PC` 形式（`CALL EXPORT PCM` が書き出すもの）。
  ボイスファイルの設定と ADPCM メモリの中身を運ぶ。OPL2EX の ADPCM と、OPNA・OPNB の
  ADPCM-B は、どれもこの同じ中身を鳴らす
- シーケンスデータにもボイスファイルの設定が入っていることがある。**両方あるときは
  `--adpcm` のほうを使う**
- 読めるのは Y8SQ 形式の版 01。FM の音色は 12 バイトのレジスタイメージで持つもの
  （Y8SQ 形式の現在の定め）で、それ以外の長さの音色を持つシーケンスは読まない
- このプレイヤーが鳴らさないデバイス番号（12 以上）のトラックは、知らせてから飛ばし、
  残りを鳴らす
- シーケンスデータがメタ情報を持っていれば使う。マスターピッチ（A4 の周波数）は全部の音の
  高さに、マスターボリュームは全部のトラックの音量に効く。タイトルと作者名は、CLI が読み込んだ
  ときに出し、GUI はトラックの一覧の上に出す。マスターピッチは 1/64 半音の刻みに丸めて効かせる
- 音量は、1 が 0.75 dB の目盛り（`V`*n* が 4*n* ＋ 67）として読む。音量を 8*n* ＋ 7 の
  目盛りで書いた古いシーケンスは、`V15` 以外が本来より小さく鳴る
- **OPNA のリズムと OPNB の ADPCM-A は鳴らない。** サンプルの中身（OPNA はチップ内蔵の
  ROM、OPNB はサンプル ROM）を読み込む手段が、このプレイヤーにまだ無いため
- OPL3・OPM・OPNA・OPNB のクロックは、それぞれ 14.31818MHz・3.579545MHz・7.9872MHz・
  8MHz

## 入手

Windows 版は [Releases](https://github.com/madscient/Y8960Sequencer/releases) の
zip にある。展開したフォルダのまま使う。エミュレータのライブラリも同梱している。
Linux と macOS は、ソースからビルドする。

## エミュレータ

次の2本の共有ライブラリを、`y8960player` と同じフォルダに置く。Windows 版の
zip には入っている。

| ライブラリ | 鳴らす音源 | Windows | Linux | macOS |
|---|---|---|---|---|
| [DSAemuEngine](https://github.com/madscient/DSAemuEngine) | SSGS、OPLLEX、OPL2EX、DCSG、SCC | `DSAemuEngine.dll` | `libDSAemuEngine.so` | `libDSAemuEngine.dylib` |
| [YMEngine](https://github.com/madscient/YMEngine) | OPL3、OPM、OPNA、OPNB | `YMFMEngine.dll` | `libYMFMEngine.so` | `libYMFMEngine.dylib` |

どちらも、ADPCM メモリを共有するための外部メモリの割り当て（FmEngineApi の
`FmEngine_SetMemoryEx`）に対応した版が要る。対応していない版を置くと、起動時にその旨を
出して止まる。

## ビルド

CMake 3.20 以上と C++17 のコンパイラが要る。

```sh
cmake -S . -B build/player -DY8960_EMULATOR_DIR=<2本のライブラリを置いたフォルダ>
cmake --build build/player --config Release
ctest --test-dir build/player -C Release --output-on-failure
```

`Y8960_EMULATOR_DIR` は省略できる。渡すと、ライブラリを実行ファイルのフォルダへ
写し、エミュレータを使う試験（`chips_test` と `render_test`）も登録する。

Windows の配布 zip は `python tools/package_windows.py <版>` で作る。2つの
エミュレータのリポジトリが、このリポジトリと同じフォルダに並んでいる前提。

SDL3 と Dear ImGui は CMake が取得する。`-DY8960_BUILD_GUI=OFF` で GUI を、
`-DY8960_BUILD_CLI=OFF` でコマンドラインを外せる。

Linux と macOS で Makefile などの単一構成のジェネレータを使うときは、`--config` の
代わりに構成時に `-DCMAKE_BUILD_TYPE=Release` を渡す。

Linux では、窓と音声出力のために、SDL3 が X11 か Wayland、および ALSA・PulseAudio・
PipeWire のいずれかの開発用パッケージが要る。一覧は
[SDL の README-linux](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies) にある。
どれも無い環境では構成が止まる。WAV の書き出しと試験だけでよければ、
`-DSDL_UNIX_CONSOLE_BUILD=ON` を渡すと窓と音声出力を持たずにビルドできる。

## ライセンス

[MIT License](LICENSE)。
