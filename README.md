# Y8960 Sequencer Player

[Y8960 BASIC Extension](https://github.com/madscient/Y8960BasicExtension) が
`CALL MSAVE` で書き出すシーケンスデータを、音源チップのエミュレータで鳴らす
スタンドアローンのプレイヤー。Windows / Linux / macOS 向け。

**開発中。** シーケンスデータを読んで鳴らせる。コマンドラインでは再生の条件
（tick の周期、繰り返し回数）をまだ引数で選べない。

## 使い方

コマンドライン。鳴らし終えると終わる。

```
y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>] [--tick <0-2>]
```

`--tick` は演奏を進める割り込みの周期で、値の意味は `CALL MINIT` の分解能と
同じ。0 が約60Hz、1 が約100Hz、2 が約200Hz、省略時は 2。**曲の速さは変わらない。**

GUI。引数は省略できる。開く・鳴らす・止める、繰り返し回数、tick の周期を選べ、
音源ブロックごと・チャンネルごとのレベルメーターが出る。リズムチャンネルは
楽器5つに分かれる。ウィンドウにファイルを落としても開く。画面の文字は英語。

```
y8960gui [シーケンスファイル] [--adpcm <ADPCM サンプルファイル>]
```

- シーケンスファイルは名前を問わない。中身の `Y8SQ` ヘッダで判断し、MSX の
  `BSAVE` で保存した見出し付きのファイルも読める
- ADPCM サンプルファイルは `Y8PC` 形式（`CALL EXPORT PCM` が書き出すもの）。
  ボイスファイルの設定と ADPCM メモリの中身を運ぶ
- シーケンスデータにもボイスファイルの設定が入っていることがある。**両方あるときは
  `--adpcm` のほうを使う**

## エミュレータ

次の3本の共有ライブラリを、`y8960player` と同じフォルダに置く。

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

SDL3 と Dear ImGui は CMake が取得する。`-DY8960_BUILD_GUI=OFF` で GUI を、
`-DY8960_BUILD_CLI=OFF` でコマンドラインを外せる。

## ライセンス

[MIT License](LICENSE)。

## ドキュメント

| | |
|---|---|
| [`doc/plan.md`](doc/plan.md) | 設計判断、見送った案、未決事項、進捗 |
| [`doc/rom-feedback.md`](doc/rom-feedback.md) | Y8960 BASIC Extension へ返す相違点と不足情報 |
