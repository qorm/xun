" Vim syntax file for XUN (X Unquoted Notation)
" Language: XUN
" Maintainer: XUN Authors
" Latest Revision: 2026-08-15

if exists("b:current_syntax")
  finish
endif

syn match xunComment "^\\s*#.*$" contains=@Spell
syn match xunKey "^\\s*[^\\s#:\\[\\]{}!-]\+\\s*:" contains=xunKeyColon
syn match xunKeyColon ":" contained
syn match xunListItem "^\\s*-\\s\+"
syn match xunTag "![a-zA-Z0-9_-]\+"
syn match xunNumber "\<[-+]\?\d\+\(_\d\+\)*\(\.\d\+\([eE][-+]\?\d\+\)\?\)\?\>"
syn match xunHex "\<0x[0-9a-fA-F_]\+\>"
syn match xunBoolean "\<\(true\|false\)\>"
syn match xunDateTime "\<\d\{4\}-\d\{2\}-\d\{2\}\(T\d\{2\}:\d\{2\}:\d\{2\}\(\.\d\+\)\?\(Z\|[+-]\d\{2\}:\d\{2\}\)\)\?\>"
syn match xunUnit "\<\d\+\(\.\d\+\)\?\(ms\|s\|m\|h\|d\|B\|KB\|MB\|GB\|TB\|KiB\|MiB\|GiB\|TiB\)\>"
syn match xunIP "\<\d\{1,3\}\.\d\{1,3\}\.\d\{1,3\}\.\d\{1,3\}\>"

syn region xunMultiline start=":\s*|.*$" end="^\s*|.*$" contains=@Spell

hi def link xunComment Comment
hi def link xunKey Identifier
hi def link xunKeyColon Operator
hi def link xunListItem Operator
hi def link xunTag Type
hi def link xunNumber Number
hi def link xunHex Number
hi def link xunBoolean Boolean
hi def link xunDateTime Constant
hi def link xunUnit Constant
hi def link xunIP Constant
hi def link xunMultiline String

let b:current_syntax = "xun"
