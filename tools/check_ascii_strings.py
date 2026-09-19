#!/usr/bin/env python3
"""src/ の C++ の文字列リテラルが ASCII だけでできていることを確かめる。

コンソールと GUI に出るメッセージは英語で ASCII に収める（doc/plan.md）。
コメントは日本語でよいので、見るのは文字列リテラルだけ。

使い方: python tools/check_ascii_strings.py [ソースの根]
見つかれば場所を出して終了コード 1 で終わる。
"""
import pathlib
import sys


def literals(text):
    """行コメント・ブロックコメントの外にある文字列リテラルを (行番号, 中身) で返す。"""
    i, line, n = 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
        elif c == "'":
            # 文字リテラル。中の " を文字列の始まりと取り違えないために飛ばす
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            yield line, text[i:j + 1]
            i = j + 1
        else:
            i += 1


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src")
    bad = 0
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        for line, lit in literals(path.read_text(encoding="utf-8")):
            if any(ord(ch) > 127 for ch in lit):
                print(f"{path}:{line}: {lit}")
                bad += 1
    if bad:
        print(f"{bad} string literal(s) contain non-ASCII characters")
        return 1
    print("all string literals are ASCII")
    return 0


sys.exit(main())
