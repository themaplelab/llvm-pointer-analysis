; RUN: opt -passes="fspa-print" < %s --disable-output | FileCheck %s -check-prefix=PL
; COM: opt -passes="fspa, print-points-to-set" < %s --disable-output | FileCheck %s -check-prefix=PPTS
; COM: opt -passes="fspa, get-alias-set" < %s --disable-output | FileCheck %s -check-prefix=GAS


define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4

; PPTS: @%retval = alloca i32, align 4
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}

; GAS: %retval = alloca i32, align 4

  %A = alloca [1 x i8], align 1

; PPTS: %A = alloca [1 x i8], align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}

; GAS: %A = alloca [1 x i8], align 1

  %B = alloca [1 x i8], align 1

; PPTS: %B = alloca [1 x i8], align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}

; GAS: %B = alloca [1 x i8], align 1

  %a = alloca ptr, align 8

; PPTS: %a = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}

; GAS: %a = alloca ptr, align 8

  %b = alloca ptr, align 8

; PPTS: %b = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}

; GAS: %b = alloca ptr, align 8

  %t1 = alloca ptr, align 8

; PPTS: %t1 = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}

; GAS: %t1 = alloca ptr, align 8

  %t2 = alloca ptr, align 8

; PPTS: %t2 = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}

; GAS: %t2 = alloca ptr, align 8

  %p = alloca ptr, align 8

; PPTS: %p = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}

; GAS: %p = alloca ptr, align 8

  %q = alloca ptr, align 8

; PPTS: %q = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}

; GAS: %q = alloca ptr, align 8

  %tmp = alloca ptr, align 8

; PPTS: %tmp = alloca ptr, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {undef}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: %tmp = alloca ptr, align 8

; PL-DAG: %retval = alloca i32, align 4 => 1
; PL-DAG: %A = alloca [1 x i8], align 1 => 1
; PL-DAG: %B = alloca [1 x i8], align 1 => 1
; PL-DAG: %a = alloca ptr, align 8 => 2
; PL-DAG: %b = alloca ptr, align 8 => 2
; PL-DAG: %t1 = alloca ptr, align 8 => 3
; PL-DAG: %t2 = alloca ptr, align 8 => 3
; PL-DAG: %p = alloca ptr, align 8 => 3
; PL-DAG: %q = alloca ptr, align 8 => 3
; PL-DAG: %tmp = alloca ptr, align 8 => 2


  store i32 0, ptr %retval, align 4

; PPTS: store i32 0, ptr %retval, align 4
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store i32 0, ptr %retval, align 4

  store ptr %a, ptr %t1, align 8

; PPTS: store ptr %a, ptr %t1, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %a, ptr %t1, align 8

  store ptr %b, ptr %t2, align 8

; PPTS: store ptr %b, ptr %t2, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %b, ptr %t2, align 8

  store ptr %A, ptr %a, align 8

; PPTS: store ptr %A, ptr %a, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %A, ptr %a, align 8

  store ptr %B, ptr %b, align 8

; PPTS: store ptr %B, ptr %b, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %B, ptr %b, align 8

  %arrayidx = getelementptr inbounds [1 x i8], ptr %A, i64 0, i64 0
  %0 = load i8, ptr %arrayidx, align 1

; PPTS: %0 = load i8, ptr %arrayidx, align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: %0 = load i8, ptr %arrayidx, align 1

  %tobool = icmp ne i8 %0, 0
  br i1 %tobool, label %if.then, label %if.else




if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %a, align 8

; PPTS: %1 = load ptr, ptr %a, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: %1 = load ptr, ptr %a, align 8

  store i8 65, ptr %1, align 1

; PPTS: store i8 65, ptr %1, align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {65}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store i8 65, ptr %1, align 1

  br label %if.end




if.else:                                          ; preds = %entry
  %2 = load ptr, ptr %t1, align 8

; PPTS: %2 = load ptr, ptr %t1, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: %2 = load ptr, ptr %t1, align 8

  store ptr %2, ptr %p, align 8

; PPTS: store ptr %2, ptr %p, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %2, ptr %p, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)

  %3 = load ptr, ptr %t2, align 8

; PPTS: %3 = load ptr, ptr %t2, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %2, ptr %p, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)

  store ptr %3, ptr %q, align 8

; PPTS: store ptr %3, ptr %q, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %3, ptr %q, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)

  %4 = load ptr, ptr %p, align 8

; PPTS: %4 = load ptr, ptr %p, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: store ptr %3, ptr %q, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)

  %5 = load ptr, ptr %4, align 8

; PPTS: %5 = load ptr, ptr %4, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {undef}

; GAS: %5 = load ptr, ptr %4, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)

  store ptr %5, ptr %tmp, align 8

; PPTS: store ptr %5, ptr %tmp, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: store ptr %5, ptr %tmp, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%tmp = alloca ptr, align 8; %a = alloca ptr, align 8)

  %6 = load ptr, ptr %q, align 8

; PPTS: %6 = load ptr, ptr %q, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %6 = load ptr, ptr %q, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%tmp = alloca ptr, align 8; %a = alloca ptr, align 8)

  %7 = load ptr, ptr %6, align 8

; PPTS: %7 = load ptr, ptr %6, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %7 = load ptr, ptr %6, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%tmp = alloca ptr, align 8; %a = alloca ptr, align 8)

  %8 = load ptr, ptr %p, align 8

; PPTS: %8 = load ptr, ptr %p, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %8 = load ptr, ptr %p, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%tmp = alloca ptr, align 8; %a = alloca ptr, align 8)

  store ptr %7, ptr %8, align 8

; PPTS: store ptr %7, ptr %8, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: store ptr %7, ptr %8, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%a = alloca ptr, align 8; %b = alloca ptr, align 8)

  %9 = load ptr, ptr %tmp, align 8

; PPTS: %9 = load ptr, ptr %tmp, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %9 = load ptr, ptr %tmp, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%a = alloca ptr, align 8; %b = alloca ptr, align 8)

  %10 = load ptr, ptr %q, align 8

; PPTS: %10 = load ptr, ptr %q, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %10 = load ptr, ptr %q, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%a = alloca ptr, align 8; %b = alloca ptr, align 8)

  store ptr %9, ptr %10, align 8

; PPTS: store ptr %9, ptr %10, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: store ptr %9, ptr %10, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  %11 = load ptr, ptr %a, align 8

; PPTS: %11 = load ptr, ptr %a, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %11 = load ptr, ptr %a, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  store i8 66, ptr %11, align 1

; PPTS: store i8 66, ptr %11, align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {66}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1}

; GAS: %11 = load ptr, ptr %a, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MUST(%b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  br label %if.end



if.end:                                           ; preds = %if.else, %if.then
  %12 = load ptr, ptr %a, align 8

; PPTS: %12 = load ptr, ptr %a, align 8
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {65; undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {66; undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8; undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8; undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; undef}

; GAS: %12 = load ptr, ptr %a, align 8
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MAY(%a = alloca ptr, align 8; %b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  store i8 63, ptr %12, align 1

; PPTS: store i8 63, ptr %12, align 1
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {65; 63; undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {66; 63; undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8; undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8; undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; undef}

; GAS: store i8 63, ptr %12, align 1
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MAY(%a = alloca ptr, align 8; %b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  %13 = load i32, ptr %retval, align 4

; PPTS: %13 = load i32, ptr %retval, align 4
; PPTS-NEXT:  %retval = alloca i32, align 4 => {0}
; PPTS-NEXT:  %A = alloca [1 x i8], align 1 => {65; 63; undef}
; PPTS-NEXT:  %B = alloca [1 x i8], align 1 => {66; 63; undef}
; PPTS-NEXT:  %a = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %b = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; %B = alloca [1 x i8], align 1}
; PPTS-NEXT:  %t1 = alloca ptr, align 8 => {%a = alloca ptr, align 8}
; PPTS-NEXT:  %t2 = alloca ptr, align 8 => {%b = alloca ptr, align 8}
; PPTS-NEXT:  %p = alloca ptr, align 8 => {%a = alloca ptr, align 8; undef}
; PPTS-NEXT:  %q = alloca ptr, align 8 => {%b = alloca ptr, align 8; undef}
; PPTS-NEXT:  %tmp = alloca ptr, align 8 => {%A = alloca [1 x i8], align 1; undef}

; GAS: %13 = load i32, ptr %retval, align 4
; GAS-NEXT: MUST(%p = alloca ptr, align 8; %t1 = alloca ptr, align 8)
; GAS-NEXT: MUST(%q = alloca ptr, align 8; %t2 = alloca ptr, align 8)
; GAS-NEXT: MAY(%a = alloca ptr, align 8; %b = alloca ptr, align 8; %tmp = alloca ptr, align 8)

  ret i32 %13
}

