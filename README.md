Aster 0.3.20 — 自動Start・出力先名・ミュート表示を修正

ZOOやStudioで録画した動画、ブラウザーの動画や音楽などを、自宅でマスタリング・エンジニアの感覚で音作りできる、Windows用の音声調整ツールです。

Chromeやメディアプレーヤーなど、アプリごとに音量を設定し、共通のエフェクトで音を整えられます。複数のアプリを1つの入力にまとめ、同じ音量で扱うこともできます。

VST2／VST3プラグインに対応しています。VST3の使用を強く推奨します。プラグインはiZotope製品、リスニングにはニアフィールドモニタースピーカーを推奨します。

お試し用に、NULL JSFX製のVST3エフェクトを2本同梱しました。

- **Auto-Leveler**：小さい音を持ち上げ、大きい音を下げて音量のばらつきを抑えます。目標音量、調整の速さ、持ち上げ・抑制の上限を設定できます。
- **Pressure**：大きな音を圧縮し、音量差を抑えるコンプレッサーです。「Amount」1つで効果の強さを調整できます。

「Add plugin」から選んで追加できます。手持ちのプラグインも使用可能です。

起動時に音声処理を開始し、初回はWindowsの既定の音声出力を使います。画面上部の出力先リストからWindowsの既定または接続済みの出力機器を選べます。選択は保存され、次回起動時にも使われます。音声のサンプルレートとバッファサイズは選んだ機器が対応する既定値を使います。

Windows x64 ZIP版とソースを配布します。音声経路の設定手順は同梱READMEをご覧ください。
## ダウンロードと使い方

Windows x64 ZIPを展開し「Aster Host.exe」を起動します。Pluginsとlicensesは同じ場所に置いてください。初回は停止状態です。

1. 再生アプリの出力を、スピーカーにつながっていない別の再生デバイスへ設定します。Asterと同じ出力から直接鳴らすと二重再生になります。
2. 初回はWindowsの既定出力を使用します。画面上部の出力先リストから、接続済みの出力機器を選べます。選択は次回起動後も保持されます。サンプルレートとバッファサイズは機器側の既定値を使います。
3. Add input／Appからアプリを選び、Add pluginでエフェクトを追加します。
4. Startを押し、Output muteを解除して音量を調整します。Saveで設定を保存します。

仮想サウンドデバイスの追加やWindowsの音声設定の自動変更は行いません。録画ファイル自体は変更しません。iZotope製品は別途入手が必要です。

## 動作環境

Windows x64、ステレオ出力、64bit VST2/VST3。Windows 10 22H2で確認しています。プロセス音声取得の可否はWindowsのバージョン・更新状況やアプリに依存します。

## ソースとビルド

GPL-3.0。配布物には同梱エフェクト等のライセンス通知を含みます。

Visual Studio C++、Windows SDK、CMake、Ninjaを用意し、Developer PowerShellから実行します。JUCE 6.1.6のソースは同じリリースのsource ZIP内third_party/JUCEに含まれます。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=third_party/JUCE
cmake --build build --target AsterHost
```

PluginsはWindows配布ZIPからビルド出力の隣へコピーしてください。

同梱エフェクト: https://github.com/koulaxizis/null-jsfx/releases/tag/3.3.1

