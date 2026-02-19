# Display Component Architecture

このドキュメントは、`components/display/include` 配下の責務境界を固定するためのガイドです。
挙動は変更せず、保守性を上げる目的でフォルダを整理しています。

## フォルダ責務

- `app/core/`
  - UI非依存の共通アプリケーション境界（IFや抽象）。
- `app/menu/`
  - ホームメニュー画面の遷移・状態変換ルール。
- `app/contact/`
  - ContactBookのドメインモデル、取得処理、操作ユースケース。
- `app/setting/`
  - SettingMenuのアクション判定、設定値読み書き、表示用ラベル生成。

- `ui/core/`
  - 画面共通の入力スナップショット・Presenter/Renderer基底。
- `ui/common/`
  - 複数画面で再利用する汎用ダイアログ/モーダル描画モデル。
- `ui/menu/`
  - ホームメニュー専用MVP。
- `ui/contact/`
  - ContactBook/MessageBox/PendingのMVP。
- `ui/setting/`
  - SettingMenuおよび設定サブダイアログのMVP。

## ファイル単位の責務

- `app/menu/navigation_usecase.hpp`
  - メニューカーソル移動、選択アクション解決、スリープ判定の純粋ロジック。
- `app/menu/status_service.hpp`
  - RSSI/電圧/通知情報の表示向け変換。

- `app/contact/domain.hpp`
  - ContactBookエンティティ生成とフィルタ条件。
- `app/contact/fetch_service.hpp`
  - BLE/HTTP経由の連絡先取得処理（入出力はContactBookドメインに限定）。
- `app/contact/actions_service.hpp`
  - 友達申請・承認/拒否などの副作用操作。
- `app/contact/menu_usecase.hpp`
  - Contact一覧での選択種別判定。
- `app/contact/menu_view_service.hpp`
  - Contact一覧行データ生成と既読反映の表示補助。

- `app/setting/action_router.hpp`
  - Setting項目キーから実行アクションへの解決。
- `app/setting/action_service.hpp`
  - 設定値トグル（振動/自動更新/開発モード）とNVS反映。
- `app/setting/task_runner.hpp`
  - 設定画面内サブタスク待機ユーティリティ。
- `app/setting/menu_label_service.hpp`
  - Setting一覧ラベル（現在値含む）の生成。
- `app/setting/menu_view_service.hpp`
  - Setting一覧の行データ組み立て。
- `app/setting/language_service.hpp`
  - 言語設定のNVS表現変換。
- `app/setting/boot_sound_service.hpp`
  - 起動音候補・表示名・プレビュー再生。
- `app/setting/bluetooth_pairing_service.hpp`
  - BLEペアリング状態管理とWi-Fi切替。
- `app/setting/firmware_info_service.hpp`
  - 稼働FW情報取得。
- `app/setting/ota_manifest_service.hpp`
  - OTA manifest URL取得と表示向け整形。

- `ui/menu/display_mvp.hpp`
  - ホームメニュー画面表示と入力解釈。
- `ui/contact/book_mvp.hpp`
  - Contact一覧画面表示とカーソル移動。
- `ui/contact/message_box_mvp.hpp`
  - メッセージ履歴表示とスクロール入力。
- `ui/contact/pending_mvp.hpp`
  - 承認待ち一覧表示と選択入力。
- `ui/contact/action_runners.hpp`
  - ContactBookの操作実行（友達追加、承認待ち一覧、承認/拒否フロー）。
- `ui/setting/menu_mvp.hpp`
  - Setting一覧表示とカーソル移動。
- `ui/setting/language_dialog.hpp`
  - 言語選択ダイアログ。
- `ui/setting/sound_settings_mvp.hpp`
  - 音量/有効状態ダイアログ。
- `ui/setting/boot_sound_dialog.hpp`
  - 起動音選択/試聴ダイアログ。
- `ui/setting/bluetooth_pairing_mvp.hpp`
  - BLEペアリング表示/入力。
- `ui/setting/firmware_info_dialog.hpp`
  - ファーム情報表示ダイアログ。
- `ui/common/confirm_dialog.hpp`
  - Yes/No確認ダイアログ。
- `ui/common/text_modal.hpp`
  - 汎用テキストモーダル。
- `ui/common/status_panel.hpp`
  - 単純2行ステータス表示。
- `ui/core/input_adapter.hpp`
  - 入力状態DTO。
- `ui/core/screen.hpp`
  - Presenter/Renderer基底インターフェース。
- `ui/core/render_context.hpp`
  - 画面サイズコンテキスト。
