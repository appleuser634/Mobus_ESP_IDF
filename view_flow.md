# 画面フロー

## テキストベース

### 初期設定時

- ※2-4の区間はback buttonで戻れるようにする

1. 起動ロゴ
2. 言語設定(日本語 or English)
3. Wi-Fi接続設定
4. ログイン選択画面(login or signup)
5. 挨拶文表示
6. ホーム画面

#### ログイン選択画面の詳細

- login
  - login_id と password を入力してログイン
- signup
  - login_id / nickname / password を入力して登録

### 通常時(ホーム画面以降)

- ホームメニュー(Talk / Box / Game)
  - Talk -> 友だち一覧 -> チャット一覧 -> メッセージ表示/送信(モールス入力)
  - Box -> 設定メニュー
  - Game -> ゲーム選択(モールス練習 / mopping.wasm)

#### 設定メニュー(主要項目)

- Profile
- Wi-Fi
- Bluetooth Pairing
- Language
- Sound/Volume
- Vibration
- Boot Sound
- RTC/モールスP2P
- Open Chat(MQTT)
- Composer
- Auto Update
- OTA Manifest
- Update Now
- Firmware Info
- Develop Mode
- Factory Reset

## Mermaid

```mermaid
flowchart TD
    A[起動ロゴ] --> B[言語設定]
    B --> C[Wi-Fi接続設定]
    C --> D{ログイン or サインアップ}
    D -->|login| E[login_id/password入力]
    D -->|signup| F[login_id/nickname/password入力]
    E --> G[挨拶文表示]
    F --> G
    G --> H[ホームメニュー]

    H --> I[Talk]
    H --> J[Box]
    H --> K[Game]

    I --> L[友だち一覧]
    L --> M[チャット一覧]
    M --> N[メッセージ表示/送信]

    K --> O{ゲーム選択}
    O --> P[モールス練習]
    O --> Q[mopping.wasm]

    J --> R[設定メニュー]
    R --> R1[Profile]
    R --> R2[Wi-Fi]
    R --> R3[Bluetooth Pairing]
    R --> R4[Language]
    R --> R5[Sound/Volume]
    R --> R6[Vibration]
    R --> R7[Boot Sound]
    R --> R8[RTC/モールスP2P]
    R --> R9[Open Chat]
    R --> R10[Composer]
    R --> R11[Auto Update]
    R --> R12[OTA Manifest]
    R --> R13[Update Now]
    R --> R14[Firmware Info]
    R --> R15[Develop Mode]
    R --> R16[Factory Reset]
```
