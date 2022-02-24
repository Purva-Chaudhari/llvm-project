// RUN: clang-repl -recovery -Xcc -fsyntax-only -Xcc -verify %s/../Sema/address-constant.c
// RUN: %clang_cc1 -fsyntax-only -verify %s/../Sema/address-packed-member-memops.c
