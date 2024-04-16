; RUN: opt -passes="fspa-print" < %s --disable-output | FileCheck %s -check-prefix=PL

define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %arr = alloca [5 x i32], align 4
  %p = alloca ptr, align 8
  %q = alloca ptr, align 8
  %i = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

; PL-DAG: %retval = alloca i32, align 4 => 1
; PL-DAG: %arr = alloca [5 x i32], align 4 => 1
; PL-DAG: %p = alloca ptr, align 8 => 2
; PL-DAG: %q = alloca ptr, align 8 => 2
; PL-DAG: %i = alloca i32, align 4 => 1


for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %i, align 4
  %cmp = icmp ne i32 %0, 3
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %arraydecay = getelementptr inbounds [5 x i32], ptr %arr, i64 0, i64 0
  %1 = load i32, ptr %i, align 4
  %idx.ext = sext i32 %1 to i64
  %add.ptr = getelementptr inbounds i32, ptr %arraydecay, i64 %idx.ext
  store ptr %add.ptr, ptr %p, align 8
  %arraydecay1 = getelementptr inbounds [5 x i32], ptr %arr, i64 0, i64 0
  %add.ptr2 = getelementptr inbounds i32, ptr %arraydecay1, i64 4
  %2 = load i32, ptr %i, align 4
  %idx.ext3 = sext i32 %2 to i64
  %idx.neg = sub i64 0, %idx.ext3
  %add.ptr4 = getelementptr inbounds i32, ptr %add.ptr2, i64 %idx.neg
  store ptr %add.ptr4, ptr %q, align 8
  %3 = load i32, ptr %i, align 4
  %4 = load ptr, ptr %p, align 8
  store i32 %3, ptr %4, align 4
  %5 = load i32, ptr %i, align 4
  %sub = sub nsw i32 4, %5
  %6 = load ptr, ptr %q, align 8
  store i32 %sub, ptr %6, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %7 = load i32, ptr %i, align 4
  %inc = add nsw i32 %7, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !6

for.end:                                          ; preds = %for.cond
  ret i32 0
}

!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}