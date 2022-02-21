// RUN: clang-repl "int i = 10;" 
// RUN: clang-repl "int i = 10;"
// RUN: clang-repl -recovery "int j = 10;"  
// RUN: clang-repl "int j = 10;"

