#pragma once

namespace notification_effects {

// 初期化処理を明示的に行いたい場合に呼び出します。
// signal_new_message() が初めて呼ばれた際にも自動初期化されます。
void init();

// MQTTで新着メッセージを受信した際に呼び出して通知効果を実行します。
void signal_new_message();

}  // namespace notification_effects

