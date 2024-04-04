; RUN: opt -passes="fspa, print-pointer-level" < %s --disable-output | FileCheck %s -check-prefix=PL
; RUN: opt -passes="fspa, print-points-to-set" < %s --disable-output | FileCheck %s -check-prefix=PPTS
; RUN: opt -passes="fspa, get-alias-set" < %s --disable-output | FileCheck %s -check-prefix=GAS


define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %A = alloca [1 x i8], align 1
  %B = alloca [1 x i8], align 1
  %a = alloca ptr, align 8
  %b = alloca ptr, align 8
  %t1 = alloca ptr, align 8
  %t2 = alloca ptr, align 8
  %p = alloca ptr, align 8
  %q = alloca ptr, align 8
  %tmp = alloca ptr, align 8

; PL: %retval = alloca i32, align 4 => 1
; PL: %A = alloca [1 x i8], align 1 => 1
; PL: %B = alloca [1 x i8], align 1 => 1
; PL: %a = alloca ptr, align 8 => 2
; PL: %b = alloca ptr, align 8 => 2
; PL: %t1 = alloca ptr, align 8 => 3
; PL: %t2 = alloca ptr, align 8 => 3
; PL: %p = alloca ptr, align 8 => 3
; PL: %q = alloca ptr, align 8 => 3
; PL: %tmp = alloca ptr, align 8 => 2

; PPTS: %retval = alloca i32, align 4 => {undef}
; PPTS: %A = alloca [1 x i8], align 1 => {undef}
; PPTS: %B = alloca [1 x i8], align 1 => {undef}
; PPTS: %a = alloca ptr, align 8 => {undef}
; PPTS: %b = alloca ptr, align 8 => {undef}
; PPTS: %t1 = alloca ptr, align 8 => {undef}
; PPTS: %t2 = alloca ptr, align 8 => {undef}
; PPTS: %p = alloca ptr, align 8 => {undef}
; PPTS: %q = alloca ptr, align 8 => {undef}
; PPTS: %tmp = alloca ptr, align 8 => {undef}

  store i32 0, ptr %retval, align 4
  store ptr %a, ptr %t1, align 8
  store ptr %b, ptr %t2, align 8
  store ptr %A, ptr %a, align 8
  store ptr %B, ptr %b, align 8
  %arrayidx = getelementptr inbounds [1 x i8], ptr %A, i64 0, i64 0
  %0 = load i8, ptr %arrayidx, align 1
  %tobool = icmp ne i8 %0, 0
  br i1 %tobool, label %if.then, label %if.else

; PPTS: %retval = alloca i32, align 4 => {0}
; PPTS: %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS: %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS: %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS: %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}



if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %a, align 8
  store i8 65, ptr %1, align 1
  br label %if.end

; PPTS: %A = alloca [1 x i8], align 1 => {65}

if.else:                                          ; preds = %entry
  %2 = load ptr, ptr %t1, align 8
  store ptr %2, ptr %p, align 8
  %3 = load ptr, ptr %t2, align 8
  store ptr %3, ptr %q, align 8
  %4 = load ptr, ptr %p, align 8
  %5 = load ptr, ptr %4, align 8
  store ptr %5, ptr %tmp, align 8
  %6 = load ptr, ptr %q, align 8
  %7 = load ptr, ptr %6, align 8
  %8 = load ptr, ptr %p, align 8
  store ptr %7, ptr %8, align 8
  %9 = load ptr, ptr %tmp, align 8
  %10 = load ptr, ptr %q, align 8
  store ptr %9, ptr %10, align 8
  %11 = load ptr, ptr %a, align 8
  store i8 66, ptr %11, align 1
  br label %if.end

; PPTS: %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS: %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS: %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS: %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS: %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS: %B = alloca [1 x i8], align 1 => {66}

; GAS: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS: MUST(%tmp = alloca ptr, align 8; %a = alloca ptr, align 8)
; GAS: MUST(%a = alloca ptr, align 8; %b = alloca ptr, align 8)


if.end:                                           ; preds = %if.else, %if.then
  %12 = load ptr, ptr %a, align 8
  store i8 63, ptr %12, align 1
  %13 = load i32, ptr %retval, align 4
  ret i32 %13
}

; PPTS: %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS: %A = alloca [1 x i8], align 1 => {63} | %B = alloca [1 x i8], align 1 => {63}

; GAS: MAY(%a = alloca ptr, align 8; %b = alloca ptr, align 8) | MAY(%a = alloca ptr, align 8; %tmp = alloca ptr, align 8)
