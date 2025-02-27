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
  %1 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 0
  store i32 0, ptr %1, align 4
  %2 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 1
  store i32 0, ptr %2, align 4
  %3 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 2
  store i32 0, ptr %3, align 4
  %4 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 3
  store i32 0, ptr %4, align 4
  %5 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 4
  store i32 0, ptr %5, align 4
  %6 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 5
  store i32 0, ptr %6, align 4
  %7 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 6
  store i32 0, ptr %7, align 4
  %8 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 7
  store i32 0, ptr %8, align 4
  %9 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 8
  store i32 0, ptr %9, align 4
  %10 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 9
  store i32 0, ptr %10, align 4
  %11 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 10
  store i32 0, ptr %11, align 4
  %12 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 11
  store i32 0, ptr %12, align 4
  %13 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 12
  store i32 0, ptr %13, align 4
  %14 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 13
  store i32 0, ptr %14, align 4
  %15 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 14
  store i32 0, ptr %15, align 4
  %16 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 15
  store i32 0, ptr %16, align 4
  br label %while.end

while.end:                                        ; preds = %0
  br label %while.cond1

while.cond1:                                      ; preds = %while.end71, %while.end
  %j.0 = phi i32 [ 0, %while.end ], [ %add62, %while.end71 ]
  %sum.0 = phi i32 [ 0, %while.end ], [ %52, %while.end71 ]
  %cmp2 = icmp slt i32 %j.0, %n
  br i1 %cmp2, label %while.body3, label %while.end72

while.body3:                                      ; preds = %while.cond1
  store i32 1, ptr %2, align 4
  store i32 2, ptr %3, align 8
  store i32 3, ptr %4, align 4
  store i32 4, ptr %5, align 16
  store i32 5, ptr %6, align 4
  store i32 6, ptr %7, align 8
  store i32 7, ptr %8, align 4
  store i32 8, ptr %9, align 16
  store i32 9, ptr %10, align 4
  store i32 10, ptr %11, align 8
  store i32 11, ptr %12, align 4
  store i32 12, ptr %13, align 16
  store i32 13, ptr %14, align 4
  store i32 14, ptr %15, align 8
  store i32 15, ptr %16, align 4
  %add62 = add nsw i32 %j.0, 1
  br label %17

17:                                               ; preds = %while.body3
  %18 = load i32, ptr %1, align 4
  %19 = add i32 %sum.0, %18
  %20 = load i32, ptr %2, align 4
  %21 = add i32 %19, %20
  %22 = load i32, ptr %3, align 4
  %23 = add i32 %21, %22
  %24 = load i32, ptr %4, align 4
  %25 = add i32 %23, %24
  %26 = load i32, ptr %5, align 4
  %27 = add i32 %25, %26
  %28 = load i32, ptr %6, align 4
  %29 = add i32 %27, %28
  %30 = load i32, ptr %7, align 4
  %31 = add i32 %29, %30
  %32 = load i32, ptr %8, align 4
  %33 = add i32 %31, %32
  %34 = load i32, ptr %9, align 4
  %35 = add i32 %33, %34
  %36 = load i32, ptr %10, align 4
  %37 = add i32 %35, %36
  %38 = load i32, ptr %11, align 4
  %39 = add i32 %37, %38
  %40 = load i32, ptr %12, align 4
  %41 = add i32 %39, %40
  %42 = load i32, ptr %13, align 4
  %43 = add i32 %41, %42
  %44 = load i32, ptr %14, align 4
  %45 = add i32 %43, %44
  %46 = load i32, ptr %15, align 4
  %47 = add i32 %45, %46
  %48 = load i32, ptr %16, align 4
  %49 = add i32 %47, %48
  br label %while.end71

while.end71:                                      ; preds = %17
  %50 = sdiv i32 %49, 1024
  %51 = shl i32 %50, 10
  %52 = sub i32 %49, %51
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
