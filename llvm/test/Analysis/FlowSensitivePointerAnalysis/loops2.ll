; ModuleID = 'loops2.bc'
source_filename = "loops2.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx14.0.0"

; Function Attrs: noinline nounwind optnone ssp uwtable
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %arr = alloca [5 x i32], align 4
  %p = alloca i32*, align 8
  %q = alloca i32*, align 8
  %i = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  store i32 0, i32* %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, i32* %i, align 4
  %cmp = icmp ne i32 %0, 3
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %arraydecay = getelementptr inbounds [5 x i32], [5 x i32]* %arr, i64 0, i64 0
  %1 = load i32, i32* %i, align 4
  %idx.ext = sext i32 %1 to i64
  %add.ptr = getelementptr inbounds i32, i32* %arraydecay, i64 %idx.ext
  store i32* %add.ptr, i32** %p, align 8
  %arraydecay1 = getelementptr inbounds [5 x i32], [5 x i32]* %arr, i64 0, i64 0
  %add.ptr2 = getelementptr inbounds i32, i32* %arraydecay1, i64 4
  %2 = load i32, i32* %i, align 4
  %idx.ext3 = sext i32 %2 to i64
  %idx.neg = sub i64 0, %idx.ext3
  %add.ptr4 = getelementptr inbounds i32, i32* %add.ptr2, i64 %idx.neg
  store i32* %add.ptr4, i32** %q, align 8
  %3 = load i32, i32* %i, align 4
  %4 = load i32*, i32** %p, align 8
  store i32 %3, i32* %4, align 4
  %5 = load i32, i32* %i, align 4
  %sub = sub nsw i32 4, %5
  %6 = load i32*, i32** %q, align 8
  store i32 %sub, i32* %6, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %7 = load i32, i32* %i, align 4
  %inc = add nsw i32 %7, 1
  store i32 %inc, i32* %i, align 4
  br label %for.cond, !llvm.loop !8

for.end:                                          ; preds = %for.cond
  ret i32 0
}

attributes #0 = { noinline nounwind optnone ssp uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-a12" "target-features"="+aes,+crc,+crypto,+fp-armv8,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+v8.3a,+zcm,+zcz" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 14, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 1, !"branch-target-enforcement", i32 0}
!3 = !{i32 1, !"sign-return-address", i32 0}
!4 = !{i32 1, !"sign-return-address-all", i32 0}
!5 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!6 = !{i32 7, !"PIC Level", i32 2}
!7 = !{!"clang version 12.0.0 (git@github.com:heturing/FSPA.git a92c83c5f0cdeffd780fe6b27fe6708fddce5329)"}
!8 = distinct !{!8, !9}
!9 = !{!"llvm.loop.mustprogress"}
