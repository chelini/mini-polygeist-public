// RUN: mini-polygeist %s | FileCheck %s

int add(int a, int b) { return a + b; }

// CHECK-LABEL: func.func @add(
// CHECK-SAME: %[[ARG0:.+]]: i32, %[[ARG1:.+]]: i32)
// CHECK: %[[ADD:.+]] = arith.addi %[[ARG0]], %[[ARG1]] : i32
// CHECK: return %[[ADD]] : i32