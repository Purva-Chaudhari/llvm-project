// RUN: clang-repl -recovery -Xcc -fsyntax-only -Xcc -verify %S/../Sema/aarch64-sve-alias-attribute.c
// RUN: clang-repl -recovery -Xcc -fsyntax-only -Xcc -verify %S/../Sema/aarch64-sve-explicit-casts-fixed-size.c
// RUN: clang-repl -recovery -Xcc -fsyntax-only -Xcc -verify %S/../Sema/address-constant.c
// RUN: clang-repl -recovery -Xcc -fsyntax-only -Xcc -verify %S/../Sema/address-packed-member-memops.c
// expected-no-diagnostics
