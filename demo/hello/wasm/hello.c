// ホストAPIの宣言 (envモジュールからインポート)
__attribute__((import_module("env"), import_name("print_char")))
void print_char(int character);

__attribute__((import_module("env"), import_name("print")))
void print_val(int val);

// エクスポートされる関数
__attribute__((export_name("hello")))
void hello(void) {
    print_char('H');
    print_char('e');
    print_char('l');
    print_char('l');
    print_char('o');
    print_char('\n');
    print_val(100);
}
