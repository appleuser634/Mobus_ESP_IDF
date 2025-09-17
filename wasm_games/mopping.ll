; ModuleID = 'BitcodeBuffer'
source_filename = "mopping"
target datalayout = "e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-i128:128-n32:64-S128-ni:1:10:20"
target triple = "wasm32-unknown-unknown-unknown"

%mopping.Vec = type { i32, i32 }

@mopping.state = internal unnamed_addr global i1 false, align 1
@mopping.blink_ms = internal unnamed_addr global i32 0, align 4
@mopping.anim_ms = internal unnamed_addr global i32 0, align 4
@mopping.scroll_x = internal unnamed_addr global i32 -160, align 4
@mopping.frame = internal unnamed_addr global i32 0, align 4
@mopping.start_time = internal unnamed_addr global i32 0, align 4
@__anon_1046 = internal unnamed_addr constant [8 x i8] c"MOPPING\00", align 1
@__anon_1058 = internal unnamed_addr constant [6 x i8] c"start\00", align 1
@0 = private unnamed_addr constant [17 x %mopping.Vec] [%mopping.Vec zeroinitializer, %mopping.Vec { i32 10, i32 50 }, %mopping.Vec { i32 20, i32 10 }, %mopping.Vec { i32 30, i32 30 }, %mopping.Vec { i32 40, i32 0 }, %mopping.Vec { i32 50, i32 50 }, %mopping.Vec { i32 60, i32 20 }, %mopping.Vec { i32 70, i32 40 }, %mopping.Vec { i32 80, i32 10 }, %mopping.Vec { i32 90, i32 50 }, %mopping.Vec { i32 100, i32 0 }, %mopping.Vec { i32 110, i32 50 }, %mopping.Vec { i32 120, i32 10 }, %mopping.Vec { i32 130, i32 30 }, %mopping.Vec { i32 140, i32 0 }, %mopping.Vec { i32 150, i32 50 }, %mopping.Vec { i32 160, i32 20 }], align 4

; Function Attrs: minsize noredzone nounwind optsize uwtable
define dso_local void @game_init() local_unnamed_addr #0 {
  store i1 false, ptr @mopping.state, align 1
  store i32 0, ptr @mopping.blink_ms, align 4
  store i32 0, ptr @mopping.anim_ms, align 4
  store i32 -160, ptr @mopping.scroll_x, align 4
  store i32 0, ptr @mopping.frame, align 4
  %1 = tail call i32 @"host_time_ms|env"()
  store i32 %1, ptr @mopping.start_time, align 4
  tail call void @"host_clear_screen|env"()
  tail call void @"host_present|env"()
  ret void
}

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare i32 @"host_time_ms|env"() local_unnamed_addr #1

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_clear_screen|env"() local_unnamed_addr #2

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_present|env"() local_unnamed_addr #3

; Function Attrs: minsize noredzone nounwind optsize uwtable
define dso_local range(i32 0, 2) i32 @game_update(i32 %0) local_unnamed_addr #0 {
  %2 = load i32, ptr @mopping.blink_ms, align 4
  %3 = add nuw i32 %2, %0
  store i32 %3, ptr @mopping.blink_ms, align 4
  %4 = load i32, ptr @mopping.anim_ms, align 4
  %5 = add nuw i32 %4, %0
  store i32 %5, ptr @mopping.anim_ms, align 4
  %6 = tail call i32 @"host_get_input|env"()
  tail call void @"host_clear_screen|env"()
  %7 = load i8, ptr @mopping.state, align 1
  %8 = trunc i8 %7 to i1
  br i1 %8, label %17, label %10

common.ret:                                       ; preds = %52, %23, %9
  %common.ret.op = phi i32 [ 0, %9 ], [ 1, %23 ], [ 1, %52 ]
  ret i32 %common.ret.op

9:                                                ; preds = %mopping.draw_running.exit, %20
  tail call void @"host_present|env"()
  tail call void @"host_sleep|env"(i32 16)
  br label %common.ret

10:                                               ; preds = %1
  %11 = load i32, ptr @mopping.blink_ms, align 4
  %12 = udiv i32 %11, 300
  %13 = and i32 %12, 1
  %14 = icmp eq i32 %13, 0
  tail call void @"host_draw_text|env"(i32 38, i32 16, ptr nonnull readonly align 1 @__anon_1046, i32 7, i32 0)
  br i1 %14, label %mopping.draw_intro.exit, label %15

15:                                               ; preds = %10
  tail call void @"host_fill_rect|env"(i32 48, i32 38, i32 40, i32 16, i32 0)
  br label %mopping.draw_intro.exit

mopping.draw_intro.exit:                          ; preds = %10, %15
  %.sink.i = phi i32 [ 0, %15 ], [ 1, %10 ]
  tail call void @"host_draw_text|env"(i32 50, i32 40, ptr nonnull readonly align 1 @__anon_1058, i32 5, i32 %.sink.i)
  %16 = and i32 %6, 3
  %.not1 = icmp eq i32 %16, 0
  br i1 %.not1, label %20, label %22

17:                                               ; preds = %1
  %18 = load i32, ptr @mopping.anim_ms, align 4
  %19 = icmp ugt i32 %18, 119
  br i1 %19, label %27, label %24

20:                                               ; preds = %mopping.draw_intro.exit, %22
  %21 = and i32 %6, 4
  %.not2 = icmp eq i32 %21, 0
  br i1 %.not2, label %9, label %23

22:                                               ; preds = %mopping.draw_intro.exit
  store i1 true, ptr @mopping.state, align 1
  store i32 0, ptr @mopping.blink_ms, align 4
  store i32 0, ptr @mopping.anim_ms, align 4
  store i32 -160, ptr @mopping.scroll_x, align 4
  store i32 0, ptr @mopping.frame, align 4
  br label %20

23:                                               ; preds = %20
  tail call void @"host_present|env"()
  br label %common.ret

24:                                               ; preds = %17, %27
  %25 = load i32, ptr @mopping.scroll_x, align 4
  %26 = icmp slt i32 %25, 180
  br i1 %26, label %50, label %30

27:                                               ; preds = %17
  store i32 0, ptr @mopping.anim_ms, align 4
  %28 = load i32, ptr @mopping.frame, align 4
  %29 = sub nuw nsw i32 1, %28
  store i32 %29, ptr @mopping.frame, align 4
  br label %24

30:                                               ; preds = %24, %50
  %31 = tail call i32 @"host_time_ms|env"()
  %32 = load i32, ptr @mopping.start_time, align 4
  tail call void @"host_draw_text|env"(i32 38, i32 4, ptr nonnull readonly align 1 @__anon_1046, i32 7, i32 0)
  br label %33

33:                                               ; preds = %42, %30
  %lsr.iv = phi i32 [ %lsr.iv.next, %42 ], [ -136, %30 ]
  %exitcond.not.i = icmp eq i32 %lsr.iv, 0
  br i1 %exitcond.not.i, label %39, label %34

34:                                               ; preds = %33
  %scevgep4 = getelementptr i8, ptr @0, i32 %lsr.iv
  %scevgep5 = getelementptr i8, ptr %scevgep4, i32 136
  %.sroa.0.0.copyload.i = load i32, ptr %scevgep5, align 4
  %35 = load i32, ptr @mopping.scroll_x, align 4
  %36 = add nsw i32 %35, %.sroa.0.0.copyload.i
  %37 = add i32 %36, 15
  %38 = icmp ult i32 %37, 143
  br i1 %38, label %43, label %42

39:                                               ; preds = %33
  %40 = load i32, ptr @mopping.scroll_x, align 4
  %41 = icmp sgt i32 %40, 64
  br i1 %41, label %45, label %mopping.draw_running.exit

42:                                               ; preds = %43, %34
  %lsr.iv.next = add nsw i32 %lsr.iv, 8
  br label %33

43:                                               ; preds = %34
  %scevgep = getelementptr i8, ptr @0, i32 %lsr.iv
  %scevgep3 = getelementptr i8, ptr %scevgep, i32 140
  %.sroa.2.0.copyload.i = load i32, ptr %scevgep3, align 4
  %44 = load i32, ptr @mopping.frame, align 4
  tail call void @"host_draw_sprite|env"(i32 0, i32 %44, i32 %36, i32 %.sroa.2.0.copyload.i)
  br label %42

45:                                               ; preds = %39
  %46 = sub nuw i32 %31, %32
  %47 = udiv i32 %46, 300
  %48 = and i32 %47, 1
  tail call void @"host_draw_text|env"(i32 50, i32 40, ptr nonnull readonly align 1 @__anon_1058, i32 5, i32 %48)
  br label %mopping.draw_running.exit

mopping.draw_running.exit:                        ; preds = %39, %45
  %49 = and i32 %6, 12
  %.not = icmp eq i32 %49, 0
  br i1 %.not, label %9, label %52

50:                                               ; preds = %24
  %51 = add nsw i32 %25, 1
  store i32 %51, ptr @mopping.scroll_x, align 4
  br label %30

52:                                               ; preds = %mopping.draw_running.exit
  tail call void @"host_present|env"()
  br label %common.ret
}

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare i32 @"host_get_input|env"() local_unnamed_addr #4

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_sleep|env"(i32) local_unnamed_addr #5

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_draw_text|env"(i32, i32, ptr nonnull readonly align 1, i32, i32) local_unnamed_addr #6

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_fill_rect|env"(i32, i32, i32, i32, i32) local_unnamed_addr #7

; Function Attrs: minsize noredzone nounwind optsize uwtable
declare void @"host_draw_sprite|env"(i32, i32, i32, i32) local_unnamed_addr #8

; Function Attrs: minsize mustprogress nofree norecurse noredzone nosync nounwind optsize willreturn memory(none) uwtable
define dso_local void @_start() local_unnamed_addr #9 {
  ret void
}

attributes #0 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," }
attributes #1 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_time_ms" }
attributes #2 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_clear_screen" }
attributes #3 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_present" }
attributes #4 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_get_input" }
attributes #5 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_sleep" }
attributes #6 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_draw_text" }
attributes #7 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_fill_rect" }
attributes #8 = { minsize noredzone nounwind optsize uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," "wasm-import-module"="env" "wasm-import-name"="host_draw_sprite" }
attributes #9 = { minsize mustprogress nofree norecurse noredzone nosync nounwind optsize willreturn memory(none) uwtable "frame-pointer"="none" "target-features"="-atomics,-bulk-memory,+bulk-memory-opt,+call-indirect-overlong,-exception-handling,+extended-const,-fp16,-multimemory,+multivalue,+mutable-globals,+nontrapping-fptoint,-reference-types,-relaxed-simd,+sign-ext,-simd128,-tail-call,-wide-arithmetic," }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6}

!0 = !{i32 1, !"wasm-feature-bulk-memory-opt", i32 43}
!1 = !{i32 1, !"wasm-feature-call-indirect-overlong", i32 43}
!2 = !{i32 1, !"wasm-feature-extended-const", i32 43}
!3 = !{i32 1, !"wasm-feature-multivalue", i32 43}
!4 = !{i32 1, !"wasm-feature-mutable-globals", i32 43}
!5 = !{i32 1, !"wasm-feature-nontrapping-fptoint", i32 43}
!6 = !{i32 1, !"wasm-feature-sign-ext", i32 43}
