; ModuleID = 'interSwap.bc'
source_filename = "interSwap.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx14.0.0"

; Function Attrs: noinline nounwind optnone ssp uwtable
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %A = alloca [1 x i8], align 1
  %B = alloca [1 x i8], align 1
  %a = alloca i8*, align 8
  %b = alloca i8*, align 8
  %t1 = alloca i8**, align 8
  %t2 = alloca i8**, align 8
  store i32 0, i32* %retval, align 4
  store i8** %a, i8*** %t1, align 8
  store i8** %b, i8*** %t2, align 8
  %0 = bitcast [1 x i8]* %A to i8*
  store i8* %0, i8** %a, align 8
  %1 = bitcast [1 x i8]* %B to i8*
  store i8* %1, i8** %b, align 8
  %arrayidx = getelementptr inbounds [1 x i8], [1 x i8]* %A, i64 0, i64 0
  %2 = load i8, i8* %arrayidx, align 1
  %tobool = icmp ne i8 %2, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %3 = load i8*, i8** %a, align 8
  store i8 65, i8* %3, align 1
  br label %if.end

if.else:                                          ; preds = %entry
  %4 = load i8**, i8*** %t1, align 8
  %5 = load i8**, i8*** %t2, align 8
  call void @swap(i8** %4, i8** %5)
  %6 = load i8*, i8** %a, align 8
  store i8 66, i8* %6, align 1
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  %7 = load i8*, i8** %a, align 8
  store i8 63, i8* %7, align 1
  %8 = load i32, i32* %retval, align 4
  ret i32 %8
}

; Function Attrs: noinline nounwind optnone ssp uwtable
define dso_local void @swap(i8** %p, i8** %q) #0 {
entry:
  %p.addr = alloca i8**, align 8
  %q.addr = alloca i8**, align 8
  %tmp = alloca i8*, align 8
  store i8** %p, i8*** %p.addr, align 8
  store i8** %q, i8*** %q.addr, align 8
  %0 = load i8**, i8*** %p.addr, align 8
  %1 = load i8*, i8** %0, align 8
  store i8* %1, i8** %tmp, align 8
  %2 = load i8**, i8*** %q.addr, align 8
  %3 = load i8*, i8** %2, align 8
  %4 = load i8**, i8*** %p.addr, align 8
  store i8* %3, i8** %4, align 8
  %5 = load i8*, i8** %tmp, align 8
  %6 = load i8**, i8*** %q.addr, align 8
  store i8* %5, i8** %6, align 8
  ret void
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
