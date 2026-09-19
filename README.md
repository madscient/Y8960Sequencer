# Y8960 Sequencer Player

[Y8960 BASIC Extension](https://github.com/madscient/Y8960BasicExtension) が
`CALL MSAVE` で書き出すシーケンスデータを、音源チップのエミュレータで鳴らす
スタンドアローンのプレイヤー。Windows / Linux / macOS 向け。

**開発中。** シーケンスデータを読んで鳴らせる。

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
| `<チップ>` | そのチップのトラック全部。チップは `SSGS` `OPLLEX1` `OPLLEX2` `OPL2EX1` `OPL2EX2` `DCSG1` `DCSG2` `SCC` |
| `<チップ>,<CH番号>` | そのチャンネル。番号はシーケンスデータと同じで、OPL2EX の 9 が ADPCM、10 がリズム |
| `T<番号>` | トラック 0-15 |
| `A`-`P` | トラック 0-15 を英字で（`A` がトラック 0） |

- 頭に `!` を付けると、指定したもの**以外**を黙らせる。`!` の付いた指定を複数
  書くと、そのどれにも当たらないものが黙る。`!` の無い指定はそのうえで黙らせる
- シーケンスが使っていないチップ・チャンネル・トラックを指定しても、何もしない
- 大文字と小文字は区別しない

GUI。引数は省略できる。開く・鳴らす・止める、繰り返し回数、tick の周期を選べ、
音源ブロックごと・チャンネルごとのレベルメーターが出る。リズムチャンネルは
楽器5つに分かれる。帯の `Mute` でチップを、バーのクリックでチャンネルを
黙らせる（リズムの楽器はチャンネル 10 の1本として黙る）。ウィンドウにファイルを落としても開く。画面の文字は英語。

```
y8960gui [シーケンスファイル] [--adpcm <ADPCM サンプルファイル>]
```

- シーケンスファイルは名前を問わない。中身の `Y8SQ` ヘッダで判断し、MSX の
  `BSAVE` で保存した見出し付きのファイルも読める
- ADPCM サンプルファイルは `Y8PC` 形式（`CALL EXPORT PCM` が書き出すもの）。
  ボイスファイルの設定と ADPCM メモリの中身を運ぶ
- シーケンスデータにもボイスファイルの設定が入っていることがある。**両方あるときは
  `--adpcm` のほうを使う**

## 入手

Windows 版は [Releases](https://github.com/madscient/Y8960Sequencer/releases) の
zip にある。展開したフォルダのまま使う。エミュレータのライブラリも同梱している。
Linux と macOS は、ソースからビルドする。

## エミュレータ

次の3本の共有ライブラリを、`y8960player` と同じフォルダに置く。Windows 版の
zip には入っている。

| ライブラリ | Windows | Linux | macOS |
|---|---|---|---|
| [Y8960emu](https://github.com/madscient/Y8960emu) | `Y8960emuEngine.dll` | `libY8960emuEngine.so` | `libY8960emuEngine.dylib` |
| [EPSGemuEngine](https://github.com/madscient/EPSGemuEngine) | `EPSGemuEngine.dll` | `libEPSGemuEngine.so` | `libEPSGemuEngine.dylib` |
| [DSAemuEngine](https://github.com/madscient/DSAemuEngine) | `DSAemuEngine.dll` | `libDSAemuEngine.so` | `libDSAemuEngine.dylib` |

## ビルド

CMake 3.20 以上と C++17 のコンパイラが要る。

```sh
cmake -S . -B build/player -DY8960_EMULATOR_DIR=<3本のライブラリを置いたフォルダ>
cmake --build build/player --config Release
ctest --test-dir build/player -C Release --output-on-failure
```

`Y8960_EMULATOR_DIR` は省略できる。渡すと、ライブラリを実行ファイルのフォルダへ
写し、エミュレータを使う試験（`chips_test` と `render_test`）も登録する。

Windows の配布 zip は `python tools/package_windows.py <版>` で作る。3つの
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
