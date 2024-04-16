; RUN: opt -passes="fspa-print" < %s --disable-output | FileCheck %s -check-prefix=PL

define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %arr = alloca [5 x i32], align 4
  %p = alloca ptr, align 8
  %i = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

; PL-DAG: %retval = alloca i32, align 4 => 1
; PL-DAG: %arr = alloca [5 x i32], align 4 => 1
; PL-DAG: %p = alloca ptr, align 8 => 2
; PL-DAG: %i = alloca i32, align 4 => 1


for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %i, align 4
  %cmp = icmp ne i32 %0, 5
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %arraydecay = getelementptr inbounds [5 x i32], ptr %arr, i64 0, i64 0
  %1 = load i32, ptr %i, align 4
  %idx.ext = sext i32 %1 to i64
  %add.ptr = getelementptr inbounds i32, ptr %arraydecay, i64 %idx.ext
  store ptr %add.ptr, ptr %p, align 8
  %2 = load i32, ptr %i, align 4
  %3 = load ptr, ptr %p, align 8
  store i32 %2, ptr %3, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %4 = load i32, ptr %i, align 4
  %inc = add nsw i32 %4, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !6

for.end:                                          ; preds = %for.cond
  ret i32 0
}

!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}