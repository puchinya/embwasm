#!/usr/bin/env python3
"""embwasm_util.py - embwasm コード生成ユーティリティ

サブコマンド:
  gen-hostapi-dispatch  WIT からホスト API ルックアップテーブル・ディスパッチャを生成
  gen-hostapi-proto     WIT からホストモジュールの HPP + CPP スケルトンを生成
"""

import os
import sys
import re
import argparse
from dataclasses import dataclass, field as dc_field
from typing import Optional, List, Tuple, Dict, Any


# ---------------------------------------------------------------------------
# WIT AST データクラス
# ---------------------------------------------------------------------------

@dataclass
class WitType:
    """WIT 型ノード"""
    kind: str   # 'primitive'|'string'|'bool'|'char'|'list'|'option'|
                # 'result'|'tuple'|'borrow'|'own'|'named'
    name: str = ''       # primitive 名 or named type 名
    params: list = None  # list/option: [WitType], tuple: [WitType,...],
                         # result: [ok_type_or_None, err_type_or_None]

    def __post_init__(self):
        if self.params is None:
            self.params = []


@dataclass
class WitFunc:
    """WIT 関数定義"""
    name: str
    params: list = None          # [(str_name, WitType)]
    results: list = None         # [(str_name_or_None, WitType)]
    is_static: bool = False
    is_constructor: bool = False

    def __post_init__(self):
        if self.params is None: self.params = []
        if self.results is None: self.results = []


@dataclass
class WitTypeDef:
    """WIT 型定義 (record / resource / enum / flags / variant / alias)"""
    name: str
    kind: str
    fields: list = None    # record: [(name, WitType)]
    cases: list = None     # enum: [str], flags: [str], variant: [(str, WitType|None)]
    alias: Any = None      # WitType (type alias)
    methods: list = None   # resource: [WitFunc]

    def __post_init__(self):
        if self.fields is None: self.fields = []
        if self.cases is None: self.cases = []
        if self.methods is None: self.methods = []


@dataclass
class WitInterface:
    """WIT インターフェイス"""
    name: str
    type_defs: dict = None   # {name: WitTypeDef}
    funcs: dict = None       # {name: WitFunc}
    annotations: dict = None

    def __post_init__(self):
        if self.type_defs is None: self.type_defs = {}
        if self.funcs is None: self.funcs = {}
        if self.annotations is None: self.annotations = {}


@dataclass
class WitDocument:
    """WIT ファイル全体"""
    package: str = ''
    interfaces: list = None
    worlds: list = None
    annotations: dict = None
    wit_imports: list = None

    def __post_init__(self):
        if self.interfaces is None: self.interfaces = []
        if self.worlds is None: self.worlds = []
        if self.annotations is None: self.annotations = {}
        if self.wit_imports is None: self.wit_imports = []


# ---------------------------------------------------------------------------
# WIT レキサー
# ---------------------------------------------------------------------------

class WitLexer:
    """WIT テキストをトークン列に変換する。
    トークン種別: IDENT, PUNCT (単一文字), ARROW (->), INT, EOF
    // および /// コメントはスキップ。
    """

    _PUNCTS = frozenset('{}()<>,;:=@')

    def __init__(self, text: str, filename: str = ''):
        self.text = text
        self.filename = filename
        self._tokens: list = []
        self._pos: int = 0
        self._tokenize()

    def _tokenize(self):
        text = self.text
        i = 0
        n = len(text)
        tokens = self._tokens
        while i < n:
            c = text[i]
            if c in ' \t\r\n':
                i += 1
                continue
            if c == '/' and i + 1 < n and text[i + 1] == '/':
                # コメント行をスキップ (/// も // も同じ)
                while i < n and text[i] != '\n':
                    i += 1
                continue
            if c == '-' and i + 1 < n and text[i + 1] == '>':
                tokens.append(('ARROW', '->'))
                i += 2
                continue
            # WIT の %keyword エスケープ: %ident → IDENT (% を除去)
            if c == '%' and i + 1 < n and (text[i + 1].isalpha() or text[i + 1] == '_'):
                j = i + 1
                while j < n and (text[j].isalnum() or text[j] in '_-'):
                    j += 1
                tokens.append(('IDENT', text[i + 1:j]))
                i = j
                continue
            if c in self._PUNCTS:
                tokens.append(('PUNCT', c))
                i += 1
                continue
            if c.isalpha() or c == '_':
                j = i
                while j < n and (text[j].isalnum() or text[j] in '_-'):
                    j += 1
                tokens.append(('IDENT', text[i:j]))
                i = j
                continue
            if c.isdigit():
                j = i
                while j < n and text[j].isdigit():
                    j += 1
                tokens.append(('INT', text[i:j]))
                i = j
                continue
            # 不明文字はスキップ
            i += 1
        tokens.append(('EOF', ''))

    def peek(self, offset: int = 0):
        pos = self._pos + offset
        return self._tokens[pos] if pos < len(self._tokens) else ('EOF', '')

    def next(self):
        tok = self._tokens[self._pos]
        if self._pos < len(self._tokens) - 1:
            self._pos += 1
        return tok

    def expect(self, kind: str, value: str = None):
        tok = self.next()
        if tok[0] != kind:
            raise SyntaxError(f'{self.filename}: expected {kind!r} got {tok!r}')
        if value is not None and tok[1] != value:
            raise SyntaxError(f'{self.filename}: expected {value!r} got {tok[1]!r}')
        return tok

    def expect_punct(self, ch: str):
        return self.expect('PUNCT', ch)

    def expect_ident(self):
        return self.expect('IDENT')

    def peek_is(self, kind: str, value: str = None) -> bool:
        tok = self.peek()
        return tok[0] == kind and (value is None or tok[1] == value)

    def peek_ident(self, value: str = None) -> bool:
        return self.peek_is('IDENT', value)

    def skip_if(self, kind: str, value: str = None) -> bool:
        if self.peek_is(kind, value):
            self.next()
            return True
        return False


# ---------------------------------------------------------------------------
# WIT パーサー
# ---------------------------------------------------------------------------

_WIT_PRIMITIVES = frozenset(
    {'s8', 'u8', 's16', 'u16', 's32', 'u32', 's64', 'u64', 'f32', 'f64'}
)


class WitParser:
    """再帰下向き構文解析で WIT テキスト → WitDocument。"""

    def __init__(self, text: str, filename: str = ''):
        self._lex = WitLexer(text, filename)
        self._filename = filename

    # ── トップレベル ──────────────────────────────────────────────────────

    def parse(self) -> WitDocument:
        doc = WitDocument()
        while not self._lex.peek_is('EOF'):
            if self._lex.peek_ident('package'):
                self._lex.next()
                doc.package = self._parse_package_name()
                self._lex.skip_if('PUNCT', ';')
            elif self._lex.peek_ident('interface'):
                self._lex.next()
                iface = self._parse_interface()
                doc.interfaces.append(iface)
            elif self._lex.peek_ident('world'):
                self._lex.next()
                world = self._parse_world()
                doc.worlds.append(world)
            else:
                self._lex.next()
        return doc

    def _parse_package_name(self) -> str:
        parts = [self._lex.expect_ident()[1]]
        while self._lex.peek_is('PUNCT', ':'):
            self._lex.next()
            parts.append(':')
            if self._lex.peek_ident():
                parts.append(self._lex.next()[1])
        while self._lex.peek_is('PUNCT', '/'):
            self._lex.next()
            parts.append('/')
            if self._lex.peek_ident():
                parts.append(self._lex.next()[1])
        return ''.join(parts)

    # ── インターフェイス ──────────────────────────────────────────────────

    def _parse_interface(self) -> WitInterface:
        name = self._lex.expect_ident()[1].replace('-', '_')
        iface = WitInterface(name=name)
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident('use'):
                self._skip_to_semi()
            elif self._lex.peek_ident('type'):
                self._lex.next()
                td = self._parse_type_alias()
                iface.type_defs[td.name] = td
            elif self._lex.peek_ident('record'):
                self._lex.next()
                n = self._lex.expect_ident()[1].replace('-', '_')
                td = self._parse_record(n)
                iface.type_defs[n] = td
            elif self._lex.peek_ident('resource'):
                self._lex.next()
                n = self._lex.expect_ident()[1].replace('-', '_')
                td = self._parse_resource(n)
                iface.type_defs[n] = td
            elif self._lex.peek_ident('enum'):
                self._lex.next()
                n = self._lex.expect_ident()[1].replace('-', '_')
                td = self._parse_enum(n)
                iface.type_defs[n] = td
            elif self._lex.peek_ident('flags'):
                self._lex.next()
                n = self._lex.expect_ident()[1].replace('-', '_')
                td = self._parse_flags(n)
                iface.type_defs[n] = td
            elif self._lex.peek_ident('variant'):
                self._lex.next()
                n = self._lex.expect_ident()[1].replace('-', '_')
                td = self._parse_variant(n)
                iface.type_defs[n] = td
            elif self._lex.peek_ident():
                fname_raw = self._lex.next()[1]
                if self._lex.peek_is('PUNCT', ':'):
                    self._lex.next()
                    if self._lex.peek_ident('func'):
                        self._lex.next()
                        func = self._parse_func_type(fname_raw.replace('-', '_'))
                        iface.funcs[func.name] = func
                    else:
                        self._skip_to_semi()
                else:
                    self._skip_to_semi()
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return iface

    # ── 型定義 ────────────────────────────────────────────────────────────

    def _parse_type_alias(self) -> WitTypeDef:
        name = self._lex.expect_ident()[1].replace('-', '_')
        self._lex.expect_punct('=')
        t = self._parse_type()
        self._lex.skip_if('PUNCT', ';')
        return WitTypeDef(name=name, kind='alias', alias=t)

    def _parse_record(self, name: str) -> WitTypeDef:
        fields = []
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident():
                fname = self._lex.next()[1].lstrip('%').replace('-', '_')
                self._lex.expect_punct(':')
                ftype = self._parse_type()
                fields.append((fname, ftype))
                self._lex.skip_if('PUNCT', ',')
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return WitTypeDef(name=name, kind='record', fields=fields)

    def _parse_resource(self, name: str) -> WitTypeDef:
        methods = []
        if not self._lex.peek_is('PUNCT', '{'):
            self._lex.skip_if('PUNCT', ';')
            return WitTypeDef(name=name, kind='resource')
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident('constructor'):
                self._lex.next()
                params = self._parse_param_list_paren()
                self._lex.skip_if('PUNCT', ';')
                methods.append(WitFunc(name='constructor', params=params,
                                       is_static=True, is_constructor=True))
            elif self._lex.peek_ident():
                mname = self._lex.next()[1].replace('-', '_')
                if self._lex.peek_is('PUNCT', ':'):
                    self._lex.next()
                    # WIT syntax: "name: static func(...)" or "name: func(...)"
                    is_static = self._lex.skip_if('IDENT', 'static')
                    if self._lex.peek_ident('func'):
                        self._lex.next()
                        func = self._parse_func_type(mname, is_static=is_static)
                        methods.append(func)
                    else:
                        self._skip_to_semi()
                else:
                    self._skip_to_semi()
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return WitTypeDef(name=name, kind='resource', methods=methods)

    def _parse_enum(self, name: str) -> WitTypeDef:
        cases = []
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident():
                cases.append(self._lex.next()[1].replace('-', '_'))
                self._lex.skip_if('PUNCT', ',')
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return WitTypeDef(name=name, kind='enum', cases=cases)

    def _parse_flags(self, name: str) -> WitTypeDef:
        cases = []
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident():
                cases.append(self._lex.next()[1].replace('-', '_'))
                self._lex.skip_if('PUNCT', ',')
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return WitTypeDef(name=name, kind='flags', cases=cases)

    def _parse_variant(self, name: str) -> WitTypeDef:
        cases = []
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident():
                cname = self._lex.next()[1].replace('-', '_')
                payload = None
                if self._lex.peek_is('PUNCT', '('):
                    self._lex.next()
                    payload = self._parse_type()
                    self._lex.expect_punct(')')
                cases.append((cname, payload))
                self._lex.skip_if('PUNCT', ',')
            else:
                self._lex.next()
        self._lex.expect_punct('}')
        return WitTypeDef(name=name, kind='variant', cases=cases)

    # ── 関数型 ────────────────────────────────────────────────────────────

    def _parse_func_type(self, name: str, is_static: bool = False) -> WitFunc:
        params = self._parse_param_list_paren()
        results = []
        if self._lex.peek_is('ARROW', '->'):
            self._lex.next()
            results = self._parse_result()
        self._lex.skip_if('PUNCT', ';')
        return WitFunc(name=name, params=params, results=results, is_static=is_static)

    def _parse_param_list_paren(self) -> list:
        self._lex.expect_punct('(')
        params = []
        while not self._lex.peek_is('PUNCT', ')') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident() and self._lex.peek(1) == ('PUNCT', ':'):
                pname = self._lex.next()[1].lstrip('%').replace('-', '_')
                self._lex.expect_punct(':')
                ptype = self._parse_type()
            else:
                pname = ''
                ptype = self._parse_type()
            params.append((pname, ptype))
            self._lex.skip_if('PUNCT', ',')
        self._lex.expect_punct(')')
        return params

    def _parse_result(self) -> list:
        # -> () means no result
        if self._lex.peek_is('PUNCT', '('):
            self._lex.next()
            if self._lex.peek_is('PUNCT', ')'):
                self._lex.next()
                return []
            results = []
            while not self._lex.peek_is('PUNCT', ')') and not self._lex.peek_is('EOF'):
                if self._lex.peek_ident() and self._lex.peek(1) == ('PUNCT', ':'):
                    rname = self._lex.next()[1].replace('-', '_')
                    self._lex.expect_punct(':')
                    rtype = self._parse_type()
                    results.append((rname, rtype))
                else:
                    rtype = self._parse_type()
                    results.append((None, rtype))
                self._lex.skip_if('PUNCT', ',')
            self._lex.expect_punct(')')
            return results
        t = self._parse_type()
        return [(None, t)]

    # ── 型参照 ────────────────────────────────────────────────────────────

    def _parse_type(self) -> WitType:
        if not self._lex.peek_ident():
            self._lex.next()
            return WitType(kind='primitive', name='u32')

        name = self._lex.next()[1]

        if name in _WIT_PRIMITIVES:
            return WitType(kind='primitive', name=name)
        if name == 'string':
            return WitType(kind='string')
        if name == 'bool':
            return WitType(kind='bool')
        if name == 'char':
            return WitType(kind='char')

        if name == 'list':
            self._lex.expect_punct('<')
            inner = self._parse_type()
            self._lex.expect_punct('>')
            return WitType(kind='list', params=[inner])

        if name == 'option':
            self._lex.expect_punct('<')
            inner = self._parse_type()
            self._lex.expect_punct('>')
            return WitType(kind='option', params=[inner])

        if name == 'result':
            if not self._lex.peek_is('PUNCT', '<'):
                return WitType(kind='result', params=[None, None])
            self._lex.next()
            params = []
            if self._lex.peek_ident('_'):
                self._lex.next(); params.append(None)
            elif not self._lex.peek_is('PUNCT', '>') and not self._lex.peek_is('PUNCT', ','):
                params.append(self._parse_type())
            else:
                params.append(None)
            if self._lex.peek_is('PUNCT', ','):
                self._lex.next()
                if self._lex.peek_ident('_'):
                    self._lex.next(); params.append(None)
                elif not self._lex.peek_is('PUNCT', '>'):
                    params.append(self._parse_type())
                else:
                    params.append(None)
            self._lex.expect_punct('>')
            return WitType(kind='result', params=params)

        if name == 'tuple':
            self._lex.expect_punct('<')
            parts = [self._parse_type()]
            while self._lex.peek_is('PUNCT', ','):
                self._lex.next()
                if self._lex.peek_is('PUNCT', '>'):
                    break
                parts.append(self._parse_type())
            self._lex.expect_punct('>')
            return WitType(kind='tuple', params=parts)

        if name == 'borrow':
            self._lex.expect_punct('<')
            inner = self._parse_type()
            self._lex.expect_punct('>')
            return WitType(kind='borrow', params=[inner])

        if name == 'own':
            self._lex.expect_punct('<')
            inner = self._parse_type()
            self._lex.expect_punct('>')
            return WitType(kind='own', params=[inner])

        # Named type (possibly with package qualifier foo/bar.type - take last part)
        return WitType(kind='named', name=name.replace('-', '_'))

    # ── ユーティリティ ────────────────────────────────────────────────────

    def _skip_to_semi(self):
        depth = 0
        while not self._lex.peek_is('EOF'):
            tok = self._lex.peek()
            if tok == ('PUNCT', '{'):
                depth += 1
            elif tok == ('PUNCT', '}'):
                if depth == 0:
                    break
                depth -= 1
            elif tok == ('PUNCT', ';') and depth == 0:
                self._lex.next()
                break
            self._lex.next()

    def _parse_world(self) -> 'WitInterface':
        name = self._lex.expect_ident()[1].replace('-', '_')
        iface = WitInterface(name=name)
        if not self._lex.peek_is('PUNCT', '{'):
            self._lex.skip_if('PUNCT', ';')
            return iface
        self._lex.expect_punct('{')
        while not self._lex.peek_is('PUNCT', '}') and not self._lex.peek_is('EOF'):
            if self._lex.peek_ident('import'):
                self._lex.next()
                if self._lex.peek_ident():
                    fname = self._lex.next()[1].replace('-', '_')
                    if self._lex.peek_is('PUNCT', ':'):
                        self._lex.next()
                        if self._lex.peek_ident('func'):
                            self._lex.next()
                            func = self._parse_func_type(fname)
                            iface.funcs[fname] = func
                        else:
                            self._skip_to_semi()
                    else:
                        self._skip_to_semi()
                else:
                    self._skip_to_semi()
            else:
                self._lex.next()
                self._skip_to_semi()
        self._lex.expect_punct('}')
        return iface

    def _skip_block_or_semi(self):
        if self._lex.peek_is('PUNCT', '{'):
            self._lex.next()
            depth = 1
            while not self._lex.peek_is('EOF') and depth > 0:
                tok = self._lex.next()
                if tok == ('PUNCT', '{'): depth += 1
                elif tok == ('PUNCT', '}'): depth -= 1
        else:
            self._skip_to_semi()


# ---------------------------------------------------------------------------
# 共通ヘルパー
# ---------------------------------------------------------------------------

_PRIMITIVE_CPP = {
    's32': 'int32_t',  'u32': 'uint32_t',
    's8':  'int8_t',   'u8':  'uint8_t',
    's16': 'int16_t',  'u16': 'uint16_t',
    's64': 'int64_t',  'u64': 'uint64_t',
    'f32': 'float',    'f64': 'double',
}

_PRIMITIVES_I32 = frozenset({'s32', 'u32', 's8', 'u8', 's16', 'u16'})
_PRIMITIVES_I64 = frozenset({'s64', 'u64'})

_WASM_TYPE_CPP = {
    'i32': 'WasmType::kI32', 'i64': 'WasmType::kI64',
    'f32': 'WasmType::kF32', 'f64': 'WasmType::kF64',
}
_WASM_FROM_FUNC = {
    'i32': 'WasmValue::FromI32', 'i64': 'WasmValue::FromI64',
    'f32': 'WasmValue::FromF32', 'f64': 'WasmValue::FromF64',
}
_WASM_CAST_CPP = {
    'i32': 'int32_t', 'i64': 'int64_t', 'f32': 'float', 'f64': 'double',
}
_WASM_VALUE_FIELD = {
    'i32': 'i32', 'i64': 'i64', 'f32': 'f32', 'f64': 'f64',
}


def _to_cpp_type_name(wit_name: str) -> str:
    """'file_handle' → 'FileHandle'"""
    return ''.join(w.capitalize() for w in wit_name.split('_') if w)


def _wit_package_to_ns(package_raw: str) -> str:
    """'embwasm:threads' → 'embwasm::threads'"""
    return package_raw.replace('-', '_').replace(':', '::').replace('/', '::')


def _list_elem_cpp(inner: WitType) -> str:
    """list<T> の要素 C++ 型"""
    if inner.kind == 'primitive':
        return _PRIMITIVE_CPP.get(inner.name, 'uint8_t')
    if inner.kind == 'bool': return 'bool'
    if inner.kind == 'char': return 'char32_t'
    return 'uint8_t'


# ---------------------------------------------------------------------------
# WIT 型 → WASM スタック型リスト
# ---------------------------------------------------------------------------

def _wit_to_wasm_types(wt: WitType, type_defs: dict) -> list:
    k = wt.kind
    if k == 'primitive':
        n = wt.name
        if n in _PRIMITIVES_I32: return ['i32']
        if n in _PRIMITIVES_I64: return ['i64']
        if n == 'f32': return ['f32']
        if n == 'f64': return ['f64']
        return ['i32']
    if k == 'bool': return ['i32']
    if k == 'char': return ['i32']
    if k in ('string', 'list'): return ['i32', 'i32']
    if k == 'option':
        return ['i32'] + _wit_to_wasm_types(wt.params[0], type_defs)
    if k == 'tuple':
        result = []
        for p in wt.params:
            result.extend(_wit_to_wasm_types(p, type_defs))
        return result
    if k == 'result':
        ok = wt.params[0] if wt.params else None
        return ['i32'] + (_wit_to_wasm_types(ok, type_defs) if ok else [])
    if k in ('borrow', 'own'):
        return _wit_to_wasm_types(wt.params[0], type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            if td.kind in ('record', 'resource', 'enum', 'flags'): return ['i32']
            if td.kind == 'variant': return ['i32', 'i32']
            if td.kind == 'alias' and td.alias:
                return _wit_to_wasm_types(td.alias, type_defs)
        return ['i32']
    return ['i32']


def _wit_is_ptr_type(wt: WitType, type_defs: dict) -> bool:
    """string / list / record は線形メモリ参照が必要"""
    k = wt.kind
    if k in ('string', 'list'): return True
    if k == 'named':
        td = type_defs.get(wt.name)
        if td and td.kind == 'record': return True
        if td and td.kind == 'alias' and td.alias:
            return _wit_is_ptr_type(td.alias, type_defs)
    if k in ('borrow', 'own'):
        return _wit_is_ptr_type(wt.params[0], type_defs)
    return False


# ---------------------------------------------------------------------------
# WIT 型 → C++ proto パラメータリスト  [(cpp_type_str, cpp_name_str)]
# ---------------------------------------------------------------------------

def _wit_to_cpp_proto_params(name: str, wt: WitType, type_defs: dict) -> list:
    k = wt.kind
    if k == 'primitive':
        return [(_PRIMITIVE_CPP.get(wt.name, 'uint32_t'), name)]
    if k == 'bool': return [('bool', name)]
    if k == 'char': return [('char32_t', name)]
    if k == 'string':
        return [('const char*', name), ('uint32_t', f'{name}_len')]
    if k == 'list':
        elem = _list_elem_cpp(wt.params[0])
        return [(f'const {elem}*', name), ('uint32_t', f'{name}_len')]
    if k == 'option':
        inner = _wit_to_cpp_proto_params(f'{name}_val', wt.params[0], type_defs)
        return [('bool', f'{name}_has_value')] + inner
    if k == 'tuple':
        result = []
        for i, p in enumerate(wt.params):
            result.extend(_wit_to_cpp_proto_params(f'{name}_{i}', p, type_defs))
        return result
    if k == 'result':
        ok = wt.params[0] if wt.params else None
        if ok is None:
            return [('bool', f'{name}_is_ok')]
        ok_params = _wit_to_cpp_proto_params(f'{name}_ok', ok, type_defs)
        return [('bool', f'{name}_is_ok')] + ok_params
    if k in ('borrow', 'own'):
        inner_wt = wt.params[0]
        if inner_wt.kind == 'named':
            td = type_defs.get(inner_wt.name)
            if td and td.kind == 'resource':
                cpp_n = _to_cpp_type_name(inner_wt.name)
                return [(f'const {cpp_n}&' if k == 'borrow' else cpp_n, name)]
        return _wit_to_cpp_proto_params(name, inner_wt, type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            cpp_n = _to_cpp_type_name(wt.name)
            if td.kind == 'record':   return [(f'const {cpp_n}*', name)]
            if td.kind == 'resource': return [(cpp_n, name)]
            if td.kind == 'enum':     return [(cpp_n, name)]
            if td.kind == 'flags':    return [('uint32_t', name)]
            if td.kind == 'variant':
                return [('int32_t', f'{name}_tag'), ('uint32_t', f'{name}_payload_ptr')]
            if td.kind == 'alias' and td.alias:
                return _wit_to_cpp_proto_params(name, td.alias, type_defs)
        return [('uint32_t', name)]
    return [('uint32_t', name)]


# ---------------------------------------------------------------------------
# WIT 型 → C++ 結果パラメータリスト  [(cpp_ref_type_str, cpp_name_str)]
# ---------------------------------------------------------------------------

def _wit_to_cpp_result_params(wt: WitType, idx: int, type_defs: dict) -> list:
    base = 'out_result' if idx == 0 else f'out_result{idx}'
    k = wt.kind
    if k == 'primitive':
        cpp = _PRIMITIVE_CPP.get(wt.name, 'uint32_t')
        return [(f'{cpp}&', base)]
    if k == 'bool': return [('bool&', base)]
    if k == 'char': return [('char32_t&', base)]
    if k in ('string', 'list'):
        return [('uint32_t&', f'{base}_ptr'), ('uint32_t&', f'{base}_len')]
    if k == 'option':
        inner = _wit_to_cpp_result_params(wt.params[0], 0, type_defs)
        renamed = [(t, f'{base}_val' if n == 'out_result' else n) for t, n in inner]
        return [('bool&', f'{base}_has_value')] + renamed
    if k == 'tuple':
        result = []
        for i, p in enumerate(wt.params):
            sub = _wit_to_cpp_result_params(p, 0, type_defs)
            result.extend((t, f'{base}_{i}') for t, _ in sub)
        return result
    if k == 'result':
        ok = wt.params[0] if wt.params else None
        if ok is None:
            return [('bool&', f'{base}_is_ok')]
        ok_params = _wit_to_cpp_result_params(ok, 0, type_defs)
        renamed = [(t, f'{base}_ok' if n == 'out_result' else n) for t, n in ok_params]
        return [('bool&', f'{base}_is_ok')] + renamed
    if k in ('borrow', 'own'):
        return _wit_to_cpp_result_params(wt.params[0], idx, type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            cpp_n = _to_cpp_type_name(wt.name)
            if td.kind == 'record':   return [('uint32_t&', f'{base}_ptr')]
            if td.kind == 'resource': return [(f'{cpp_n}&', base)]
            if td.kind == 'enum':     return [(f'{cpp_n}&', base)]
            if td.kind == 'flags':    return [('uint32_t&', base)]
            if td.kind == 'variant':
                return [('int32_t&', f'{base}_tag'), ('uint32_t&', f'{base}_payload_ptr')]
            if td.kind == 'alias' and td.alias:
                return _wit_to_cpp_result_params(td.alias, idx, type_defs)
        return [('uint32_t&', base)]
    return [('uint32_t&', base)]


# ---------------------------------------------------------------------------
# WIT 型 → dispatch 用ローカル変数 C++ 型
# ---------------------------------------------------------------------------

def _wit_to_out_var_type(wt: WitType, type_defs: dict) -> str:
    k = wt.kind
    if k == 'primitive': return _PRIMITIVE_CPP.get(wt.name, 'uint32_t')
    if k == 'bool': return 'bool'
    if k == 'char': return 'char32_t'
    if k in ('string', 'list'): return 'uint32_t'
    if k in ('borrow', 'own'): return _wit_to_out_var_type(wt.params[0], type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            cpp_n = _to_cpp_type_name(wt.name)
            if td.kind == 'record':   return 'uint32_t'
            if td.kind == 'resource': return cpp_n
            if td.kind == 'enum':     return cpp_n
            if td.kind == 'flags':    return 'uint32_t'
            if td.kind == 'variant':  return 'int32_t'
            if td.kind == 'alias' and td.alias:
                return _wit_to_out_var_type(td.alias, type_defs)
    return 'uint32_t'


# ---------------------------------------------------------------------------
# dispatch 用: スタックポップ行リスト生成
# ---------------------------------------------------------------------------

def _gen_param_pop_lines(name: str, wt: WitType, indent: str,
                         type_defs: dict) -> list:
    """WASM スタックから name を pop する C++ 行リストを返す (top-of-stack first)。"""
    k = wt.kind
    if k == 'primitive':
        n = wt.name
        cpp = _PRIMITIVE_CPP.get(n, 'uint32_t')
        if n in _PRIMITIVES_I32:
            return [f'{indent}{cpp} _{name} = static_cast<{cpp}>(ctx->stack[--ctx->stack_top].value.i32);']
        if n in _PRIMITIVES_I64:
            return [f'{indent}{cpp} _{name} = static_cast<{cpp}>(ctx->stack[--ctx->stack_top].value.i64);']
        if n == 'f32':
            return [f'{indent}float _{name} = ctx->stack[--ctx->stack_top].value.f32;']
        if n == 'f64':
            return [f'{indent}double _{name} = ctx->stack[--ctx->stack_top].value.f64;']
        return [f'{indent}{cpp} _{name} = static_cast<{cpp}>(ctx->stack[--ctx->stack_top].value.i32);']
    if k == 'bool':
        return [f'{indent}bool _{name} = static_cast<bool>(ctx->stack[--ctx->stack_top].value.i32);']
    if k == 'char':
        return [f'{indent}char32_t _{name} = static_cast<char32_t>(ctx->stack[--ctx->stack_top].value.i32);']
    if k in ('string', 'list'):
        return [
            f'{indent}uint32_t _{name}_len = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);',
            f'{indent}uint32_t _{name}_ptr = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);',
        ]
    if k == 'option':
        # Stack (bottom→top): [has_value, T...]  → pop T slots first, then has_value
        inner_lines = _gen_param_pop_lines(f'{name}_val', wt.params[0], indent, type_defs)
        return inner_lines + [
            f'{indent}bool _{name}_has_value = static_cast<bool>(ctx->stack[--ctx->stack_top].value.i32);',
        ]
    if k == 'tuple':
        # Pop in reverse order
        lines = []
        for i in range(len(wt.params) - 1, -1, -1):
            lines.extend(_gen_param_pop_lines(f'{name}_{i}', wt.params[i], indent, type_defs))
        return lines
    if k == 'result':
        ok = wt.params[0] if wt.params else None
        if ok:
            ok_lines = _gen_param_pop_lines(f'{name}_ok', ok, indent, type_defs)
        else:
            ok_lines = []
        return ok_lines + [
            f'{indent}bool _{name}_is_ok = static_cast<bool>(ctx->stack[--ctx->stack_top].value.i32);',
        ]
    if k in ('borrow', 'own'):
        return _gen_param_pop_lines(name, wt.params[0], indent, type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            cpp_n = _to_cpp_type_name(wt.name)
            if td.kind == 'record':
                return [f'{indent}uint32_t _{name}_ptr = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);']
            if td.kind == 'resource':
                return [f'{indent}{cpp_n} _{name}(static_cast<int32_t>(ctx->stack[--ctx->stack_top].value.i32));']
            if td.kind == 'enum':
                return [f'{indent}{cpp_n} _{name} = static_cast<{cpp_n}>(ctx->stack[--ctx->stack_top].value.i32);']
            if td.kind == 'flags':
                return [f'{indent}uint32_t _{name} = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);']
            if td.kind == 'variant':
                return [
                    f'{indent}uint32_t _{name}_payload_ptr = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);',
                    f'{indent}int32_t _{name}_tag = static_cast<int32_t>(ctx->stack[--ctx->stack_top].value.i32);',
                ]
            if td.kind == 'alias' and td.alias:
                return _gen_param_pop_lines(name, td.alias, indent, type_defs)
    return [f'{indent}uint32_t _{name} = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);']


# ---------------------------------------------------------------------------
# dispatch 用: 呼び出し引数リスト生成
# ---------------------------------------------------------------------------

def _gen_call_args(name: str, wt: WitType, type_defs: dict) -> list:
    """C++ 呼び出し引数式リストを返す。"""
    k = wt.kind
    if k in ('primitive', 'bool', 'char', 'flags'):
        return [f'_{name}']
    if k == 'string':
        return [f'reinterpret_cast<const char*>(_mem + _{name}_ptr), _{name}_len']
    if k == 'list':
        elem = _list_elem_cpp(wt.params[0])
        return [f'reinterpret_cast<const {elem}*>(_mem + _{name}_ptr), _{name}_len']
    if k == 'option':
        inner_args = _gen_call_args(f'{name}_val', wt.params[0], type_defs)
        return [f'_{name}_has_value'] + inner_args
    if k == 'tuple':
        result = []
        for i, p in enumerate(wt.params):
            result.extend(_gen_call_args(f'{name}_{i}', p, type_defs))
        return result
    if k == 'result':
        ok = wt.params[0] if wt.params else None
        if ok:
            ok_args = _gen_call_args(f'{name}_ok', ok, type_defs)
        else:
            ok_args = []
        return [f'_{name}_is_ok'] + ok_args
    if k in ('borrow', 'own'):
        return _gen_call_args(name, wt.params[0], type_defs)
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            if td.kind == 'record':   return [f'reinterpret_cast<const {_to_cpp_type_name(wt.name)}*>(_mem + _{name}_ptr)']
            if td.kind in ('resource', 'enum'): return [f'_{name}']
            if td.kind == 'flags':    return [f'_{name}']
            if td.kind == 'variant':  return [f'_{name}_tag', f'_{name}_payload_ptr']
            if td.kind == 'alias' and td.alias:
                return _gen_call_args(name, td.alias, type_defs)
    return [f'_{name}']


# ---------------------------------------------------------------------------
# dispatch 用: 結果プッシュ行リスト生成
# ---------------------------------------------------------------------------

def _gen_result_push_lines(wt: WitType, var_name: str, indent: str,
                           type_defs: dict) -> list:
    """結果変数を WASM スタックにプッシュする行リスト。"""
    k = wt.kind
    if k == 'primitive':
        n = wt.name
        if n in _PRIMITIVES_I64:
            return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI64(static_cast<int64_t>({var_name}));']
        if n == 'f32':
            return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromF32({var_name});']
        if n == 'f64':
            return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromF64({var_name});']
        return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(static_cast<int32_t>({var_name}));']
    if k in ('bool', 'char'):
        return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(static_cast<int32_t>({var_name}));']
    if k == 'named':
        td = type_defs.get(wt.name)
        if td:
            if td.kind == 'resource':
                return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32({var_name}.handle);']
            if td.kind == 'enum':
                return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(static_cast<int32_t>({var_name}));']
            if td.kind in ('record', 'flags'):
                return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(static_cast<int32_t>({var_name}));']
            if td.kind == 'alias' and td.alias:
                return _gen_result_push_lines(td.alias, var_name, indent, type_defs)
    # default: treat as i32
    return [f'{indent}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(static_cast<int32_t>({var_name}));']


# ---------------------------------------------------------------------------
# C++ 型宣言生成 (struct / enum class / class)
# ---------------------------------------------------------------------------

def _gen_cpp_type_decls(type_defs: dict) -> str:
    """type_defs から C++ struct / enum class / class 宣言文字列を生成する。"""
    lines = []
    for name, td in type_defs.items():
        cpp_n = _to_cpp_type_name(name)
        if td.kind == 'record':
            lines.append(f'struct {cpp_n} {{')
            for fname, ftype in td.fields:
                fparams = _wit_to_cpp_proto_params(fname, ftype, type_defs)
                for cpp_t, cpp_fn in fparams:
                    # strip const / pointer for struct members
                    member_t = cpp_t.replace('const ', '').replace('*', '').strip()
                    lines.append(f'    {member_t} {cpp_fn};')
            lines.append('};')
        elif td.kind == 'enum':
            lines.append(f'enum class {cpp_n} : int32_t {{')
            for i, case in enumerate(td.cases):
                case_cpp = _to_cpp_type_name(case)
                lines.append(f'    {case_cpp} = {i},')
            lines.append('};')
        elif td.kind == 'flags':
            lines.append(f'// flags {cpp_n}')
            for i, bit in enumerate(td.cases):
                bit_cpp = _to_cpp_type_name(bit)
                lines.append(f'constexpr uint32_t k{cpp_n}{bit_cpp} = 1u << {i};')
        elif td.kind == 'resource':
            lines.append(f'class {cpp_n} {{')
            lines.append('public:')
            lines.append('    int32_t handle;')
            lines.append(f'    constexpr {cpp_n}() noexcept : handle(-1) {{}}')
            lines.append(f'    constexpr explicit {cpp_n}(int32_t h) noexcept : handle(h) {{}}')
            # Methods
            for m in td.methods:
                if m.is_constructor:
                    cpp_params = ['WasmEngine& engine']
                    for pn, pt in m.params:
                        for ct, cn in _wit_to_cpp_proto_params(pn or 'arg', pt, type_defs):
                            cpp_params.append(f'{ct} {cn}')
                    lines.append(f'    WasmResult Construct({", ".join(cpp_params)}) noexcept;')
                elif m.is_static:
                    mname_cpp = _to_cpp_type_name(m.name)
                    cpp_params = ['WasmEngine& engine']
                    for pn, pt in m.params:
                        for ct, cn in _wit_to_cpp_proto_params(pn or 'arg', pt, type_defs):
                            cpp_params.append(f'{ct} {cn}')
                    for idx, (_, rt) in enumerate(m.results):
                        for ct, cn in _wit_to_cpp_result_params(rt, idx, type_defs):
                            cpp_params.append(f'{ct} {cn}')
                    lines.append(f'    static WasmResult {mname_cpp}({", ".join(cpp_params)}) noexcept;')
                else:
                    mname_cpp = _to_cpp_type_name(m.name)
                    cpp_params = ['WasmEngine& engine']
                    for pn, pt in m.params:
                        for ct, cn in _wit_to_cpp_proto_params(pn or 'arg', pt, type_defs):
                            cpp_params.append(f'{ct} {cn}')
                    for idx, (_, rt) in enumerate(m.results):
                        for ct, cn in _wit_to_cpp_result_params(rt, idx, type_defs):
                            cpp_params.append(f'{ct} {cn}')
                    lines.append(f'    WasmResult {mname_cpp}({", ".join(cpp_params)}) noexcept;')
            lines.append('};')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# gen-hostapi-dispatch コード生成
# ---------------------------------------------------------------------------

def _gen_dispatch_case(api: dict, const_name: str) -> str:
    i = '            '
    type_defs = api.get('type_defs', {})
    param_names = api.get('param_names', [])
    param_wit_types = api.get('param_wit_types', [])
    result_wit_types = api.get('result_wit_types', [])
    cpp_func = api['function']
    method_kind = api.get('method_kind')

    needs_mem = any(_wit_is_ptr_type(t, type_defs) for t in param_wit_types)

    lines = []

    # Resource method: constructor
    if method_kind == 'constructor':
        res_cpp = api.get('resource_cpp_name', 'Resource')
        for j in range(len(param_wit_types) - 1, -1, -1):
            name = param_names[j] if j < len(param_names) else f'arg{j}'
            lines.extend(_gen_param_pop_lines(name, param_wit_types[j], i, type_defs))
        if needs_mem:
            lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')
        lines.append(f'{i}{res_cpp} _result;')
        call_args = ['engine']
        for name, wt in zip(param_names, param_wit_types):
            call_args.extend(_gen_call_args(name, wt, type_defs))
        lines.append(f'{i}WasmResult res = _result.Construct({", ".join(call_args)});')
        lines.append(f'{i}if (res != WasmResult::kOk) return res;')
        lines.append(f'{i}ctx->stack[ctx->stack_top++] = WasmValue::FromI32(_result.handle);')
        lines.append(f'{i}return res;')
        body = '\n'.join(lines)
        return f'        case {const_name}: {{\n{body}\n        }}'

    # Resource method: instance
    if method_kind == 'method':
        res_cpp = api.get('resource_cpp_name', 'Resource')
        method_cpp = api.get('method_cpp_name', 'Method')
        # Pop params in reverse (self is first logical param → pop last)
        for j in range(len(param_wit_types) - 1, -1, -1):
            name = param_names[j] if j < len(param_names) else f'arg{j}'
            lines.extend(_gen_param_pop_lines(name, param_wit_types[j], i, type_defs))
        # Pop self handle (last to pop = bottom of stack)
        lines.append(f'{i}{res_cpp} _self(static_cast<int32_t>(ctx->stack[--ctx->stack_top].value.i32));')
        if needs_mem:
            lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')
        for idx, rt in enumerate(result_wit_types):
            out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
            lines.append(f'{i}{_wit_to_out_var_type(rt, type_defs)} {out_name}{{}};')
        call_args = ['engine']
        for name, wt in zip(param_names, param_wit_types):
            call_args.extend(_gen_call_args(name, wt, type_defs))
        for idx in range(len(result_wit_types)):
            call_args.append('_out_result' if idx == 0 else f'_out_result{idx}')
        lines.append(f'{i}WasmResult res = _self.{method_cpp}({", ".join(call_args)});')
        if result_wit_types:
            lines.append(f'{i}if (res == WasmResult::kYield) return res;')
            lines.append(f'{i}if (res != WasmResult::kOk) return res;')
            for idx, rt in enumerate(result_wit_types):
                out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
                lines.extend(_gen_result_push_lines(rt, out_name, i, type_defs))
        lines.append(f'{i}return res;')
        body = '\n'.join(lines)
        return f'        case {const_name}: {{\n{body}\n        }}'

    # Resource method: static
    if method_kind == 'static':
        res_cpp = api.get('resource_cpp_name', 'Resource')
        method_cpp = api.get('method_cpp_name', 'Method')
        for j in range(len(param_wit_types) - 1, -1, -1):
            name = param_names[j] if j < len(param_names) else f'arg{j}'
            lines.extend(_gen_param_pop_lines(name, param_wit_types[j], i, type_defs))
        if needs_mem:
            lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')
        for idx, rt in enumerate(result_wit_types):
            out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
            lines.append(f'{i}{_wit_to_out_var_type(rt, type_defs)} {out_name}{{}};')
        call_args = ['engine']
        for name, wt in zip(param_names, param_wit_types):
            call_args.extend(_gen_call_args(name, wt, type_defs))
        for idx in range(len(result_wit_types)):
            call_args.append('_out_result' if idx == 0 else f'_out_result{idx}')
        lines.append(f'{i}WasmResult res = {res_cpp}::{method_cpp}({", ".join(call_args)});')
        if result_wit_types:
            lines.append(f'{i}if (res == WasmResult::kYield) return res;')
            lines.append(f'{i}if (res != WasmResult::kOk) return res;')
            for idx, rt in enumerate(result_wit_types):
                out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
                lines.extend(_gen_result_push_lines(rt, out_name, i, type_defs))
        lines.append(f'{i}return res;')
        body = '\n'.join(lines)
        return f'        case {const_name}: {{\n{body}\n        }}'

    # Regular function
    for j in range(len(param_wit_types) - 1, -1, -1):
        name = param_names[j] if j < len(param_names) else f'arg{j}'
        lines.extend(_gen_param_pop_lines(name, param_wit_types[j], i, type_defs))

    if needs_mem:
        lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')

    for idx, rt in enumerate(result_wit_types):
        out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
        lines.append(f'{i}{_wit_to_out_var_type(rt, type_defs)} {out_name}{{}};')

    call_args = ['engine']
    for name, wt in zip(param_names, param_wit_types):
        call_args.extend(_gen_call_args(name, wt, type_defs))
    for idx in range(len(result_wit_types)):
        call_args.append('_out_result' if idx == 0 else f'_out_result{idx}')

    lines.append(f'{i}WasmResult res = {cpp_func}({", ".join(call_args)});')

    if result_wit_types:
        lines.append(f'{i}if (res == WasmResult::kYield) return res;')
        lines.append(f'{i}if (res != WasmResult::kOk) return res;')
        for idx, rt in enumerate(result_wit_types):
            out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
            lines.extend(_gen_result_push_lines(rt, out_name, i, type_defs))

    lines.append(f'{i}return res;')
    body = '\n'.join(lines)
    return f'        case {const_name}: {{\n{body}\n        }}'


def _gen_validate_case(api: dict, const_name: str) -> str:
    i = '        '
    type_defs = api.get('type_defs', {})
    param_wit_types = api.get('param_wit_types', [])
    result_wit_types = api.get('result_wit_types', [])
    method_kind = api.get('method_kind')

    wasm_params = []
    if method_kind == 'method':
        wasm_params.append('i32')  # self handle
    for t in param_wit_types:
        wasm_params.extend(_wit_to_wasm_types(t, type_defs))

    wasm_results = []
    if method_kind == 'constructor':
        wasm_results.append('i32')  # returned handle
    else:
        for t in result_wit_types:
            wasm_results.extend(_wit_to_wasm_types(t, type_defs))

    lines = [f'{i}case {const_name}: {{']
    lines.append(f'{i}    if (sig->param_count != {len(wasm_params)} || sig->result_count != {len(wasm_results)}) return false;')

    checks = []
    for idx, wt in enumerate(wasm_params):
        checks.append(f'sig->GetParam({idx}) == {_WASM_TYPE_CPP[wt]}')
    for idx, wt in enumerate(wasm_results):
        checks.append(f'sig->GetResult({idx}) == {_WASM_TYPE_CPP[wt]}')

    if checks:
        lines.append(f'{i}    return {" && ".join(checks)};')
    else:
        lines.append(f'{i}    return true;')
    lines.append(f'{i}}}')
    return '\n'.join(lines)


def _gen_typed_protos(apis: list) -> str:
    ns_protos: dict = {}
    for api in apis:
        type_defs = api.get('type_defs', {})
        cpp_func = api['function']
        method_kind = api.get('method_kind')
        # Skip resource member methods - they're in the class declaration
        if method_kind in ('constructor', 'method'):
            continue
        parts = cpp_func.rsplit('::', 1)
        ns = parts[0] if len(parts) == 2 else ''
        func_name = parts[-1]

        cpp_params = ['WasmEngine& engine']
        for name, wt in zip(api.get('param_names', []), api.get('param_wit_types', [])):
            for ct, cn in _wit_to_cpp_proto_params(name, wt, type_defs):
                cpp_params.append(f'{ct} {cn}')
        for idx, rt in enumerate(api.get('result_wit_types', [])):
            for ct, cn in _wit_to_cpp_result_params(rt, idx, type_defs):
                cpp_params.append(f'{ct} {cn}')
        proto = f'WasmResult {func_name}({", ".join(cpp_params)}) noexcept;'
        ns_protos.setdefault(ns, []).append(proto)

    blocks = []
    for ns in sorted(ns_protos.keys()):
        protos = ns_protos[ns]
        if ns == 'embwasm':
            rel_ns = ''
        elif ns.startswith('embwasm::'):
            rel_ns = ns[len('embwasm::'):]
        else:
            rel_ns = ns
        if rel_ns:
            ns_parts = rel_ns.split('::')
            open_ns = '\n'.join(f'namespace {p} {{' for p in ns_parts)
            close_ns = '\n'.join(f'}} // namespace {p}' for p in reversed(ns_parts))
            block = open_ns + '\n' + '\n'.join(protos) + '\n' + close_ns
        else:
            block = '\n'.join(protos)
        blocks.append(block)
    return '\n\n'.join(blocks)


# ---------------------------------------------------------------------------
# WIT ファイル → api dict リスト変換ヘルパー
# ---------------------------------------------------------------------------

def _wit_package_to_import_module(package: str, iface_name_raw: str) -> str:
    """'embwasm:stdio', 'stdio' → 'embwasm:stdio/stdio'"""
    if package and iface_name_raw:
        return f'{package}/{iface_name_raw}'
    return '$root'


def _funcs_from_interface(iface: WitInterface, pkg_ns: str,
                          import_module: str, module_init, module_deinit,
                          iface_name: str, module_name_override: str = None) -> list:
    """WitInterface からフラット API エントリリストを生成する。"""
    entries = []
    type_defs = iface.type_defs
    module_key = module_name_override if module_name_override is not None else iface_name

    # Free functions
    for fname, func in iface.funcs.items():
        if pkg_ns and iface_name:
            cpp_func = f'embwasm::hostmodules::{pkg_ns}::{iface_name}::{fname}'
        else:
            continue
        param_names = [p[0] or f'arg{i}' for i, p in enumerate(func.params)]
        param_wit_types = [p[1] for p in func.params]
        result_wit_types = [r[1] for r in func.results]
        entries.append({
            'module': module_key,
            'field': fname,
            'wit_field': fname.replace('_', '-'),
            'function': cpp_func,
            'import_module': import_module,
            'param_names': param_names,
            'param_wit_types': param_wit_types,
            'result_wit_types': result_wit_types,
            'init': module_init,
            'deinit': module_deinit,
            'type_defs': type_defs,
        })

    # Resource methods → dispatch entries
    for res_name, td in type_defs.items():
        if td.kind != 'resource':
            continue
        res_cpp = _to_cpp_type_name(res_name)
        res_name_raw = res_name.replace('_', '-')
        if pkg_ns and iface_name:
            ns_prefix = f'embwasm::hostmodules::{pkg_ns}::{iface_name}'
        else:
            continue

        for m in td.methods:
            if m.is_constructor:
                field = f'[constructor]{res_name_raw}'
                cpp_func = f'{ns_prefix}::{res_cpp}::Construct'
                param_names = [p[0] or f'arg{i}' for i, p in enumerate(m.params)]
                param_wit_types = [p[1] for p in m.params]
                entries.append({
                    'module': iface_name,
                    'field': field,
                    'wit_field': field,
                    'function': cpp_func,
                    'import_module': import_module,
                    'param_names': param_names,
                    'param_wit_types': param_wit_types,
                    'result_wit_types': [],
                    'init': module_init,
                    'deinit': module_deinit,
                    'type_defs': type_defs,
                    'method_kind': 'constructor',
                    'resource_cpp_name': res_cpp,
                    'resource_name': res_name,
                })
            elif m.is_static:
                mname_cpp = _to_cpp_type_name(m.name)
                field = f'[static]{res_name_raw}.{m.name.replace("_", "-")}'
                cpp_func = f'{ns_prefix}::{res_cpp}::{mname_cpp}'
                param_names = [p[0] or f'arg{i}' for i, p in enumerate(m.params)]
                param_wit_types = [p[1] for p in m.params]
                result_wit_types = [r[1] for r in m.results]
                entries.append({
                    'module': iface_name,
                    'field': field,
                    'wit_field': field,
                    'function': cpp_func,
                    'import_module': import_module,
                    'param_names': param_names,
                    'param_wit_types': param_wit_types,
                    'result_wit_types': result_wit_types,
                    'init': module_init,
                    'deinit': module_deinit,
                    'type_defs': type_defs,
                    'method_kind': 'static',
                    'resource_cpp_name': res_cpp,
                    'method_cpp_name': mname_cpp,
                    'resource_name': res_name,
                })
            else:
                mname_cpp = _to_cpp_type_name(m.name)
                field = f'[method]{res_name_raw}.{m.name.replace("_", "-")}'
                cpp_func = f'{ns_prefix}::{res_cpp}::{mname_cpp}'
                param_names = [p[0] or f'arg{i}' for i, p in enumerate(m.params)]
                param_wit_types = [p[1] for p in m.params]
                result_wit_types = [r[1] for r in m.results]
                entries.append({
                    'module': iface_name,
                    'field': field,
                    'wit_field': field,
                    'function': cpp_func,
                    'import_module': import_module,
                    'param_names': param_names,
                    'param_wit_types': param_wit_types,
                    'result_wit_types': result_wit_types,
                    'init': module_init,
                    'deinit': module_deinit,
                    'type_defs': type_defs,
                    'method_kind': 'method',
                    'resource_cpp_name': res_cpp,
                    'method_cpp_name': mname_cpp,
                    'resource_name': res_name,
                })
    return entries


# ---------------------------------------------------------------------------
# WIT ファイルパーサー (旧 _parse_wit / _parse_wit_for_proto の置き換え)
# ---------------------------------------------------------------------------

def _extract_annotations(content: str) -> dict:
    """/// doc コメントからアノテーションを抽出する。"""
    anns: dict = {}
    headers = re.findall(r'^///\s*@cpp-header:\s*["\']?([\w./-]+)["\']?',
                         content, re.MULTILINE)
    if headers:
        anns['cpp-header'] = headers
    wit_imports = re.findall(r'^///\s*@wit-import:\s*["\']?([\w./-]+\.wit)["\']?',
                             content, re.MULTILINE)
    if wit_imports:
        anns['wit-import'] = wit_imports
    m = re.search(r'^///.*@cpp-init:\s*([\w:]+)', content, re.MULTILINE)
    if m:
        anns['cpp-init'] = m.group(1)
    m = re.search(r'^///.*@cpp-deinit:\s*([\w:]+)', content, re.MULTILINE)
    if m:
        anns['cpp-deinit'] = m.group(1)
    return anns


def _parse_wit(wit_path: str):
    """WIT ファイルをパースして (imports, headers, apis_flat) を返す。"""
    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()

    anns = _extract_annotations(content)
    headers = anns.get('cpp-header', [])
    wit_imports = anns.get('wit-import', [])

    doc = WitParser(content, wit_path).parse()

    apis_flat = []
    pkg_ns = _wit_package_to_ns(doc.package) if doc.package else ''
    module_init = anns.get('cpp-init')
    module_deinit = anns.get('cpp-deinit')

    for iface in doc.interfaces:
        iface_name = iface.name
        iface_name_raw = iface.name.replace('_', '-')
        import_module = _wit_package_to_import_module(doc.package, iface_name_raw)
        entries = _funcs_from_interface(
            iface, pkg_ns, import_module, module_init, module_deinit, iface_name)
        apis_flat.extend(entries)

    # world ブロックの import func → module="env" ($root) として登録
    for world in doc.worlds:
        entries = _funcs_from_interface(
            world, pkg_ns, '$root', module_init, module_deinit,
            world.name, module_name_override='env')
        apis_flat.extend(entries)

    return wit_imports, headers, apis_flat


def _parse_wit_for_proto(wit_path: str):
    """proto 生成に必要な情報を返す: (package_raw, interface_raw, headers, type_defs, funcs)"""
    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()

    anns = _extract_annotations(content)
    headers = anns.get('cpp-header', [])

    doc = WitParser(content, wit_path).parse()

    if not doc.interfaces:
        return doc.package or 'unknown:unknown', 'unknown', headers, {}, []

    iface = doc.interfaces[0]
    funcs = list(iface.funcs.values())
    return doc.package or 'unknown:unknown', iface.name, headers, iface.type_defs, funcs


# ---------------------------------------------------------------------------
# マルチファイル読み込み
# ---------------------------------------------------------------------------

def load_all_configs(entry_path: str):
    """エントリポイント WIT から imports を辿り、全設定をマージして返す。"""
    merged_headers = []
    seen_headers: set = set()
    merged_apis = []
    seen_api_keys: set = set()
    seen_modules: dict = {}

    entry_abs = os.path.abspath(entry_path)
    stack = [entry_abs]
    visited: set = set()

    while stack:
        current_path = stack.pop()
        if current_path in visited:
            print(f"Warning: Circular import detected, skipping '{current_path}'",
                  file=sys.stderr)
            continue
        visited.add(current_path)

        if not os.path.exists(current_path):
            print(f"Error: Configuration file '{current_path}' not found.",
                  file=sys.stderr)
            sys.exit(1)

        if not current_path.endswith('.wit'):
            print(f"Error: Only WIT files are supported. Got: '{current_path}'",
                  file=sys.stderr)
            sys.exit(1)

        current_dir = os.path.dirname(current_path)
        file_imports, file_headers, file_apis = _parse_wit(current_path)

        file_modules = set(api.get('module', '') for api in file_apis)
        for m in file_modules:
            if m in seen_modules and seen_modules[m] != current_path:
                print(f"Error: Duplicate module '{m}' in '{current_path}' "
                      f"(already in '{seen_modules[m]}').", file=sys.stderr)
                sys.exit(1)
            seen_modules[m] = current_path

        for h in file_headers:
            if h not in seen_headers:
                seen_headers.add(h)
                merged_headers.append(h)

        for api in file_apis:
            key = (api.get('module', ''), api.get('field', ''))
            if key not in seen_api_keys:
                seen_api_keys.add(key)
                merged_apis.append(api)

        for import_rel in reversed(file_imports):
            import_abs = os.path.normpath(os.path.join(current_dir, import_rel))
            if import_abs not in visited:
                stack.append(import_abs)

    return merged_headers, merged_apis


# ---------------------------------------------------------------------------
# gen-hostapi-dispatch
# ---------------------------------------------------------------------------

def cmd_gen_hostapi_dispatch(args):
    config_path = args.wit_file
    out_h_path = args.out_hpp
    out_cpp_path = args.out_cpp

    if not os.path.exists(config_path):
        print(f"Error: Configuration file '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    headers, apis = load_all_configs(config_path)
    apis.sort(key=lambda x: (x['module'], x['field']))
    modules = sorted(list(set(api['module'] for api in apis)))

    module_inits, module_deinits = {}, {}
    for api in apis:
        m = api['module']
        if api.get('init'): module_inits[m] = api['init']
        if api.get('deinit'): module_deinits[m] = api['deinit']

    init_calls_str = '\n'.join(
        f'    {module_inits[m]}(engine);' for m in modules if m in module_inits)
    deinit_calls_str = '\n'.join(
        f'    {module_deinits[m]}(engine);' for m in modules if m in module_deinits)

    module_enum_members_str = '\n'.join(
        f'    k{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]", "_", m).split("_") if w)} = {i},'
        for i, m in enumerate(modules))

    enum_members = []
    for idx, api in enumerate(apis):
        mod_part = ''.join(w.capitalize() for w in re.sub(r'[^a-zA-Z0-9_]', '_', api['module']).split('_') if w)
        field_part = ''.join(w.capitalize() for w in re.sub(r'[^a-zA-Z0-9_]', '_', api['field']).split('_') if w)
        enum_members.append(
            f'constexpr HostFunctionId kWasmHostFuncId{mod_part}{field_part} = '
            f'static_cast<HostFunctionId>({idx});')
    enum_members_str = '\n'.join(enum_members)

    cpp_entries_str = ',\n'.join(
        f'    {{ "{api["import_module"]}", "{api.get("wit_field") or api["field"]}", kWasmHostFuncId'
        f'{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]","_",api["module"]).split("_") if w)}'
        f'{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]","_",api["field"]).split("_") if w)}'
        f' }}'
        for api in apis)

    module_lookup_str = '\n'.join(
        f'    if (module_len == {len(m)} && std::memcmp(module_name, "{m}", {len(m)}) == 0) '
        f'return HostModuleId::k{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]","_",m).split("_") if w)};'
        for m in modules)

    dispatch_cases_str = '\n'.join(
        _gen_dispatch_case(api, em.split('=')[0].replace('constexpr HostFunctionId', '').strip())
        for api, em in zip(apis, enum_members))

    validate_cases_str = '\n'.join(
        _gen_validate_case(api, em.split('=')[0].replace('constexpr HostFunctionId', '').strip())
        for api, em in zip(apis, enum_members))

    typed_protos_str = _gen_typed_protos(apis)
    include_directives_str = '\n'.join(f'#include "{h}"' for h in headers)

    lookup_logic = """\
    if ((module_len == 5 && std::memcmp(module_name, "$root", 5) == 0) || (module_len == 3 && std::memcmp(module_name, "env", 3) == 0)) {
        for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
            std::size_t entry_field_len = std::strlen(kStaticApiTable[i].field_name);
            if (field_len == entry_field_len && std::memcmp(field_name, kStaticApiTable[i].field_name, field_len) == 0) {
                return kStaticApiTable[i].id;
            }
        }
    } else {
        int low = 0;
        int high = static_cast<int>(kStaticApiTableSize) - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            const auto& entry = kStaticApiTable[mid];
            std::size_t entry_mod_len = std::strlen(entry.module_name);
            std::size_t min_mod_len = (module_len < entry_mod_len) ? module_len : entry_mod_len;
            int cmp = std::memcmp(module_name, entry.module_name, min_mod_len);
            if (cmp == 0) {
                if (module_len < entry_mod_len) cmp = -1;
                else if (module_len > entry_mod_len) cmp = 1;
                else {
                    std::size_t entry_field_len = std::strlen(entry.field_name);
                    std::size_t min_field_len = (field_len < entry_field_len) ? field_len : entry_field_len;
                    cmp = std::memcmp(field_name, entry.field_name, min_field_len);
                    if (cmp == 0) {
                        if (field_len < entry_field_len) cmp = -1;
                        else if (field_len > entry_field_len) cmp = 1;
                    }
                }
            }
            if (cmp == 0) return entry.id;
            else if (cmp < 0) high = mid - 1;
            else low = mid + 1;
        }
    }
    bool is_root = ((module_len == 5 && std::memcmp(module_name, "$root", 5) == 0) || (module_len == 3 && std::memcmp(module_name, "env", 3) == 0));
    for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
        std::size_t entry_mod_len = std::strlen(kStaticApiTable[i].module_name);
        if (is_root || (module_len == entry_mod_len && std::memcmp(module_name, kStaticApiTable[i].module_name, module_len) == 0)) {
            const char* s2 = kStaticApiTable[i].field_name;
            std::size_t s2_len = std::strlen(s2);
            if (field_len == s2_len) {
                bool match = true;
                for (std::size_t j = 0; j < field_len; ++j) {
                    char c1 = field_name[j]; char c2 = s2[j];
                    if (c1 == '-') c1 = '_'; if (c2 == '-') c2 = '_';
                    if (c1 != c2) { match = false; break; }
                }
                if (match) return kStaticApiTable[i].id;
            }
        }
    }
    return HostFunctionId::kInvalid;"""

    h_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-dispatch] - DO NOT EDIT DIRECTLY
// =============================================================================

#ifndef EMBWASM_WASM_API_STATIC_HPP_
#define EMBWASM_WASM_API_STATIC_HPP_

#include "wasm_api.hpp"

namespace embwasm {{

// HostModuleId 定数
enum class HostModuleId : uint32_t {{
{module_enum_members_str}
}};

// HostFunctionId 定数
{enum_members_str}

HostModuleId LookupStaticHostModuleId(const char* module_name, std::size_t module_len) noexcept;
HostFunctionId LookupStaticHostFunctionId(const char* module_name, std::size_t module_len, const char* field_name, std::size_t field_len) noexcept;

class WasmEngine;
struct WasmThreadContext;

void InitializeAllHostModules(WasmEngine& engine) noexcept;
void DeinitializeAllHostModules(WasmEngine& engine) noexcept;

WasmResult DispatchHostFunction(WasmEngine& engine, HostFunctionId id, WasmThreadContext* ctx) noexcept;
bool ValidateHostFunctionType(HostFunctionId id, const WasmTypeSignature* sig) noexcept;

// ---------------------------------------------------------------------------
// Typed host function declarations (auto-generated from WIT)
// ---------------------------------------------------------------------------
{typed_protos_str}

}} // namespace embwasm

#endif // EMBWASM_WASM_API_STATIC_HPP_
"""

    cpp_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-dispatch] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"
{include_directives_str}
#include <cstring>

namespace embwasm {{

extern const std::size_t kHostModuleCount = {len(modules)};

void InitializeAllHostModules(WasmEngine& engine) noexcept {{
    (void)engine;
{init_calls_str}
}}

void DeinitializeAllHostModules(WasmEngine& engine) noexcept {{
    (void)engine;
{deinit_calls_str}
}}

HostModuleId LookupStaticHostModuleId(const char* module_name, std::size_t module_len) noexcept {{
{module_lookup_str}
    return static_cast<HostModuleId>(0xFFFFFFFF);
}}

struct StaticApiEntry {{
    const char* module_name;
    const char* field_name;
    HostFunctionId id;
}};

static const StaticApiEntry kStaticApiTable[] = {{
{cpp_entries_str}
}};
static constexpr std::size_t kStaticApiTableSize = sizeof(kStaticApiTable) / sizeof(kStaticApiTable[0]);

HostFunctionId LookupStaticHostFunctionId(const char* module_name, std::size_t module_len, const char* field_name, std::size_t field_len) noexcept {{
{lookup_logic}
}}

WasmResult DispatchHostFunction(WasmEngine& engine, HostFunctionId id, WasmThreadContext* ctx) noexcept {{
    switch (id) {{
{dispatch_cases_str}
        default:
            return WasmResult::kErrorExecuteRuntimeError;
    }}
}}

bool ValidateHostFunctionType(HostFunctionId id, const WasmTypeSignature* sig) noexcept {{
    switch (id) {{
{validate_cases_str}
        default:
            return false;
    }}
}}

}} // namespace embwasm
"""

    os.makedirs(os.path.dirname(os.path.abspath(out_cpp_path)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(out_h_path)), exist_ok=True)

    with open(out_h_path, 'w', encoding='utf-8') as f:
        f.write(h_content)
    with open(out_cpp_path, 'w', encoding='utf-8') as f:
        f.write(cpp_content)

    print(f"Generated {out_h_path} and {out_cpp_path} from {config_path}")


# ---------------------------------------------------------------------------
# gen-hostapi-proto ヘルパー
# ---------------------------------------------------------------------------

_DECL_BEGIN   = '// [embwasm-proto:decl-begin]'
_DECL_END     = '// [embwasm-proto:decl-end]'
_TYPES_BEGIN  = '// [embwasm-proto:types-begin]'
_TYPES_END    = '// [embwasm-proto:types-end]'
_FUNCS_END    = '// [embwasm-proto:funcs-end]'


def _build_func_decl(func: WitFunc, type_defs: dict) -> str:
    """関数宣言文字列を生成する。"""
    cpp_params = ['WasmEngine& engine']
    for pn, pt in func.params:
        for ct, cn in _wit_to_cpp_proto_params(pn or 'arg', pt, type_defs):
            cpp_params.append(f'{ct} {cn}')
    for idx, (_, rt) in enumerate(func.results):
        for ct, cn in _wit_to_cpp_result_params(rt, idx, type_defs):
            cpp_params.append(f'{ct} {cn}')
    return f'WasmResult {func.name}({", ".join(cpp_params)}) noexcept;'


def _build_proto_hpp(package_raw: str, interface_raw: str,
                     type_defs: dict, funcs: list, guard_macro: str) -> str:
    pkg_ns = _wit_package_to_ns(package_raw)
    iface_ns = interface_raw.replace('-', '_')
    inner_parts = pkg_ns.split('::') + [iface_ns]
    open_inner = '\n'.join(f'namespace {p} {{' for p in inner_parts)
    close_inner = '\n'.join(f'}} // namespace {p}' for p in reversed(inner_parts))

    type_decls = _gen_cpp_type_decls(type_defs)
    decls = '\n'.join(_build_func_decl(f, type_defs) for f in funcs)

    return f"""\
// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-proto] - DO NOT EDIT DIRECTLY
// =============================================================================

#ifndef {guard_macro}
#define {guard_macro}

#include "wasm_types.hpp"

namespace embwasm {{
class WasmEngine;

namespace hostmodules {{
{open_inner}

{_TYPES_BEGIN}
{type_decls}
{_TYPES_END}

{_DECL_BEGIN}
{decls}
{_DECL_END}

{close_inner}
}} // namespace hostmodules
}} // namespace embwasm

#endif // {guard_macro}
"""


def _build_proto_cpp_stub(func: WitFunc, type_defs: dict,
                          is_member: bool = False,
                          class_name: str = '') -> str:
    """単一関数スタブ文字列（マーカー付き）を返す。"""
    cpp_params = ['WasmEngine& engine']
    void_args = ['(void)engine;']
    for pn, pt in func.params:
        for ct, cn in _wit_to_cpp_proto_params(pn or 'arg', pt, type_defs):
            cpp_params.append(f'{ct} {cn}')
            void_args.append(f'(void){cn};')
    for idx, (_, rt) in enumerate(func.results):
        for ct, cn in _wit_to_cpp_result_params(rt, idx, type_defs):
            cpp_params.append(f'{ct} {cn}')
            void_args.append(f'(void){cn};')
    void_str = ' '.join(void_args)

    if is_member and class_name:
        marker_name = f'{class_name}_{func.name}'
        if func.is_constructor:
            sig = f'WasmResult {class_name}::Construct({", ".join(cpp_params)}) noexcept'
        elif func.is_static:
            mname = _to_cpp_type_name(func.name)
            sig = f'WasmResult {class_name}::{mname}({", ".join(cpp_params)}) noexcept'
        else:
            mname = _to_cpp_type_name(func.name)
            sig = f'WasmResult {class_name}::{mname}({", ".join(cpp_params)}) noexcept'
    else:
        marker_name = func.name
        sig = f'WasmResult {func.name}({", ".join(cpp_params)}) noexcept'

    return (f'// [embwasm-proto:func:{marker_name}]\n'
            f'{sig} {{\n    {void_str}\n    return WasmResult::kErrorExecuteRuntimeError;\n}}')


def _build_proto_cpp(package_raw: str, interface_raw: str,
                     type_defs: dict, funcs: list, hpp_filename: str) -> str:
    pkg_ns = _wit_package_to_ns(package_raw)
    iface_ns = interface_raw.replace('-', '_')
    inner_parts = pkg_ns.split('::') + [iface_ns]
    open_inner = '\n'.join(f'namespace {p} {{' for p in inner_parts)
    close_inner = '\n'.join(f'}} // namespace {p}' for p in reversed(inner_parts))

    stub_parts = []
    # Free function stubs
    for func in funcs:
        stub_parts.append(_build_proto_cpp_stub(func, type_defs))
    # Resource method stubs
    for res_name, td in type_defs.items():
        if td.kind != 'resource':
            continue
        cpp_n = _to_cpp_type_name(res_name)
        for m in td.methods:
            stub_parts.append(_build_proto_cpp_stub(m, type_defs,
                                                    is_member=True, class_name=cpp_n))

    stubs_str = '\n\n'.join(stub_parts)

    return f"""\
// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-proto] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "{hpp_filename}"
#include "wasm_engine.hpp"

namespace embwasm {{
namespace hostmodules {{
{open_inner}

{stubs_str}

{_FUNCS_END}
{close_inner}
}} // namespace hostmodules
}} // namespace embwasm
"""


def _update_proto_hpp(existing: str, new_type_decls: str, new_decls_block: str) -> Optional[str]:
    """既存 HPP の types/decl マーカー間を更新する。"""
    # Update types block if markers exist
    tb = existing.find(_TYPES_BEGIN)
    te = existing.find(_TYPES_END)
    if tb != -1 and te != -1 and te > tb:
        existing = (existing[:tb]
                    + _TYPES_BEGIN + '\n'
                    + new_type_decls + '\n'
                    + _TYPES_END
                    + existing[te + len(_TYPES_END):])

    # Update decl block
    begin_idx = existing.find(_DECL_BEGIN)
    end_idx = existing.find(_DECL_END)
    if begin_idx == -1 or end_idx == -1 or end_idx <= begin_idx:
        return None
    return (existing[:begin_idx]
            + _DECL_BEGIN + '\n'
            + new_decls_block + '\n'
            + _DECL_END
            + existing[end_idx + len(_DECL_END):])


def _get_known_cpp_funcs(content: str) -> set:
    return set(re.findall(r'//\s*\[embwasm-proto:func:(\w+)\]', content))


def _update_proto_cpp(existing_content: str, new_funcs: list,
                      type_defs: dict) -> Optional[str]:
    """既存 CPP を差分更新する。"""
    if _FUNCS_END not in existing_content:
        return None

    known = _get_known_cpp_funcs(existing_content)
    # Collect all expected markers
    new_names: set = set()
    for f in new_funcs:
        new_names.add(f.name)
    for res_name, td in type_defs.items():
        if td.kind != 'resource':
            continue
        cpp_n = _to_cpp_type_name(res_name)
        for m in td.methods:
            new_names.add(f'{cpp_n}_{m.name}')

    result = existing_content

    # Mark deleted functions as obsolete
    for fname in sorted(known - new_names):
        old_tag = f'// [embwasm-proto:func:{fname}]'
        new_tag = (f'// [embwasm-proto:obsolete:{fname}]'
                   f' -- WIT から削除されました。実装を確認後に削除してください。')
        result = result.replace(old_tag, new_tag, 1)

    # Insert new stubs before funcs-end
    added_stubs = []
    for f in new_funcs:
        if f.name not in known:
            added_stubs.append(_build_proto_cpp_stub(f, type_defs))
    for res_name, td in type_defs.items():
        if td.kind != 'resource':
            continue
        cpp_n = _to_cpp_type_name(res_name)
        for m in td.methods:
            marker = f'{cpp_n}_{m.name}'
            if marker not in known:
                added_stubs.append(_build_proto_cpp_stub(
                    m, type_defs, is_member=True, class_name=cpp_n))

    if added_stubs:
        stubs = '\n\n'.join(added_stubs)
        result = result.replace(_FUNCS_END, stubs + '\n\n' + _FUNCS_END, 1)

    return result


# ---------------------------------------------------------------------------
# gen-hostapi-proto
# ---------------------------------------------------------------------------

def cmd_gen_hostapi_proto(args):
    wit_path = args.wit_file
    out_h_path = args.out_hpp
    out_cpp_path = args.out_cpp

    if not os.path.exists(wit_path):
        print(f"Error: WIT file '{wit_path}' not found.", file=sys.stderr)
        sys.exit(1)

    package_raw, interface_raw, _headers, type_defs, funcs = _parse_wit_for_proto(wit_path)

    hpp_basename = os.path.basename(out_h_path)
    guard_macro = re.sub(r'[^A-Z0-9]', '_', hpp_basename.upper()) + '_'

    # Build decls block for HPP
    new_type_decls = _gen_cpp_type_decls(type_defs)
    decls_lines = [_build_func_decl(f, type_defs) for f in funcs]
    new_decls_block = '\n'.join(decls_lines)

    if os.path.exists(out_h_path):
        with open(out_h_path, 'r', encoding='utf-8') as f:
            existing_hpp = f.read()
        hpp_content = _update_proto_hpp(existing_hpp, new_type_decls, new_decls_block)
        if hpp_content is None:
            print(f"Warning: no proto markers in {out_h_path}, overwriting.", file=sys.stderr)
            hpp_content = _build_proto_hpp(package_raw, interface_raw,
                                           type_defs, funcs, guard_macro)
    else:
        hpp_content = _build_proto_hpp(package_raw, interface_raw,
                                       type_defs, funcs, guard_macro)

    if os.path.exists(out_cpp_path):
        with open(out_cpp_path, 'r', encoding='utf-8') as f:
            existing_cpp = f.read()
        cpp_content = _update_proto_cpp(existing_cpp, funcs, type_defs)
        if cpp_content is None:
            print(f"Warning: no proto markers in {out_cpp_path}, overwriting.", file=sys.stderr)
            cpp_content = _build_proto_cpp(package_raw, interface_raw,
                                           type_defs, funcs, hpp_basename)
    else:
        cpp_content = _build_proto_cpp(package_raw, interface_raw,
                                       type_defs, funcs, hpp_basename)

    os.makedirs(os.path.dirname(os.path.abspath(out_h_path)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(out_cpp_path)), exist_ok=True)

    with open(out_h_path, 'w', encoding='utf-8') as f:
        f.write(hpp_content)
    with open(out_cpp_path, 'w', encoding='utf-8') as f:
        f.write(cpp_content)

    print(f"Generated {out_h_path} and {out_cpp_path} from {wit_path}")


# ---------------------------------------------------------------------------
# エントリポイント
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        prog='embwasm_util.py',
        description='embwasm コード生成ユーティリティ',
    )
    subparsers = parser.add_subparsers(dest='subcommand', required=True)

    p_dispatch = subparsers.add_parser(
        'gen-hostapi-dispatch',
        help='WIT からホスト API ルックアップテーブル・ディスパッチャを生成',
    )
    p_dispatch.add_argument('wit_file', help='入力 WIT ファイル')
    p_dispatch.add_argument('out_hpp', help='出力 HPP パス')
    p_dispatch.add_argument('out_cpp', help='出力 CPP パス')
    p_dispatch.set_defaults(func=cmd_gen_hostapi_dispatch)

    p_proto = subparsers.add_parser(
        'gen-hostapi-proto',
        help='WIT からホストモジュール HPP + CPP スケルトンを生成',
    )
    p_proto.add_argument('wit_file', help='入力 WIT ファイル')
    p_proto.add_argument('out_hpp', help='出力 HPP パス')
    p_proto.add_argument('out_cpp', help='出力 CPP パス')
    p_proto.set_defaults(func=cmd_gen_hostapi_proto)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
