; RUN: opt -passes="fspa-print" < %s --disable-output | FileCheck %s -check-prefix=PL

; Function Attrs: noinline nounwind ssp uwtable(sync)
define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %i = alloca i32, align 4
  %p = alloca ptr, align 8
  %q = alloca ptr, align 8
  store i32 0, ptr %retval, align 4
  store i32 0, ptr %i, align 4
  store ptr %i, ptr %p, align 8
  store ptr %i, ptr %q, align 8
  %0 = load ptr, ptr %p, align 8
  store i32 1, ptr %0, align 4
  ret i32 0
}


; PL-DAG: %retval = alloca i32, align 4 => 1
; PL-DAG: %i = alloca i32, align 4 => 1
; PL-DAG: %p = alloca ptr, align 8 => 2
; PL-DAG: %q = alloca ptr, align 8 => 2


