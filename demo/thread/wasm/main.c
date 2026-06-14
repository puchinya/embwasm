#include "wasm_api.h"

// スレッド2の本体
__attribute__((export_name("thread2")))
void thread2(void) {
    for (int i = 0; i < 3; ++i) {
        print_char('T');
        print_char('2');
        print_char(':');
        print_val(i);
        print_char('\n');
        thread_yield();
    }
    // イベント1をシグナルして終了
    event_signal(1);
}

// エクスポートされるメイン関数
__attribute__((export_name("main")))
void main(void) {
    print_char('M');
    print_char('a');
    print_char('i');
    print_char('n');
    print_char('\n');

    // スレッド2を起動 (thread2のインデックス 6 を指定)
    // 注: ビルド環境によってインデックスが変わる可能性があるため、
    // 本来はシンボル名で解決するか、動的に取得する必要があります。
    thread_spawn(6);

    for (int i = 0; i < 3; ++i) {
        print_char('M');
        print_char('1');
        print_char(':');
        print_val(i);
        print_char('\n');
        thread_yield();
    }

    // スレッド2の終了を待つ
    print_char('W');
    print_char('a');
    print_char('i');
    print_char('t');
    print_char('\n');
    event_wait(1);

    print_char('D');
    print_char('o');
    print_char('n');
    print_char('e');
    print_char('\n');
}
