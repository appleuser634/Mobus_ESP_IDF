# フォント生成（x14y24pxHeadUpDaisy → LovyanGFX用 u8g2）

このプロジェクトでは、IME（日本語入力モード）表示用に `x14y24pxHeadUpDaisy.ttf` を u8g2形式フォントへ変換して組み込んでいます。

## 生成手順

- サブセット範囲（含める文字）は `tools/fonts/headupdaisy_subset.map` を編集します。
- フォント生成は以下を実行します：

`./tools/fonts/gen_headupdaisy_u8g2.sh`

生成物は以下に出力されます。

- `components/Display/src/font_headupdaisy14x16.cpp`（16px相当）
- `components/Display/src/font_headupdaisy14x8.cpp`（8px相当）
