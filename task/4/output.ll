; ModuleID = '/home/linzhy/SYsU-lang2/build/test/task3/performance/if-combine1.sysu.c/answer.ll'
source_filename = "/home/linzhy/SYsU-lang2/test/cases/performance/if-combine1.sysu.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind optnone
define dso_local i32 @func(i32 noundef %n) #0 {
entry:
  %s = alloca [16 x i32], align 16
  br label %0

0:                                                ; preds = %entry
  br label %while.end

while.end:                                        ; preds = %0
  br label %while.cond1

while.cond1:                                      ; preds = %while.end71, %while.end
  %j.0 = phi i32 [ 0, %while.end ], [ %add62, %while.end71 ]
  %sum.0 = phi i32 [ 0, %while.end ], [ %20, %while.end71 ]
  %cmp2 = icmp slt i32 %j.0, %n
  br i1 %cmp2, label %while.body3, label %while.end72

while.body3:                                      ; preds = %while.cond1
  %add62 = add nsw i32 %j.0, 1
  br label %1

1:                                                ; preds = %while.body3
  %2 = add i32 %sum.0, 0
  %3 = add i32 %2, 1
  %4 = add i32 %3, 2
  %5 = add i32 %4, 3
  %6 = add i32 %5, 4
  %7 = add i32 %6, 5
  %8 = add i32 %7, 6
  %9 = add i32 %8, 7
  %10 = add i32 %9, 8
  %11 = add i32 %10, 9
  %12 = add i32 %11, 10
  %13 = add i32 %12, 11
  %14 = add i32 %13, 12
  %15 = add i32 %14, 13
  %16 = add i32 %15, 14
  %17 = add i32 %16, 15
  br label %while.end71

while.end71:                                      ; preds = %1
  %18 = sdiv i32 %17, 1024
  %19 = shl i32 %18, 10
  %20 = sub i32 %17, %19
  br label %while.cond1, !llvm.loop !2

while.end72:                                      ; preds = %while.cond1
  ret i32 %sum.0
}

; Function Attrs: noinline nounwind optnone
define dso_local i32 @main() #0 {
entry:
  call void @_sysy_starttime(i32 noundef 73)
  %call = call i32 (...) @_sysy_getint()
  %call1 = call i32 @func(i32 noundef %call)
  call void @_sysy_putint(i32 noundef %call1)
  call void @_sysy_putch(i32 noundef 10)
  call void @_sysy_stoptime(i32 noundef 77)
  ret i32 0
}

declare void @_sysy_starttime(i32 noundef %0) #1

declare i32 @_sysy_getint(...) #1

declare void @_sysy_putint(i32 noundef %0) #1

declare void @_sysy_putch(i32 noundef %0) #1

declare void @_sysy_stoptime(i32 noundef %0) #1

attributes #0 = { noinline nounwind optnone "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }
attributes #1 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 17.0.6 (https://github.com/arcsysu/SYsU-lang2.git d442ff5455e6ab1f59d9cf7952a1df9763b27c2e)"}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.mustprogress"}
