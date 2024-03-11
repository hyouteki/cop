; ModuleID = 'nullcheck5.bc'
source_filename = "nullcheck5.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@0 = private unnamed_addr constant [57 x i8] c"error: null pointer dereference found in function `foo`\0A\00", align 1
@1 = private unnamed_addr constant [58 x i8] c"error: null pointer dereference found in function `main`\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @foo(i32* %arr) #0 {
entry:
  %arr.addr = alloca i32*, align 8
  %ptr = alloca i32*, align 8
  %IsNull = icmp eq i32** %arr.addr, null
  br i1 %IsNull, label %NullBB, label %NotNullBB

NotNullBB:                                        ; preds = %entry
  store i32* %arr, i32** %arr.addr, align 8
  %call = call i8* @mymalloc(i32 4)
  %0 = bitcast i8* %call to i32*
  %IsNull2 = icmp eq i32** %ptr, null
  br i1 %IsNull2, label %NullBB, label %NotNullBB1

NotNullBB1:                                       ; preds = %NotNullBB
  store i32* %0, i32** %ptr, align 8
  %IsNull4 = icmp eq i32** %ptr, null
  br i1 %IsNull4, label %NullBB, label %NotNullBB3

NotNullBB3:                                       ; preds = %NotNullBB1
  %1 = load i32*, i32** %ptr, align 8
  %IsNull6 = icmp eq i32* %1, null
  br i1 %IsNull6, label %NullBB, label %NotNullBB5

NotNullBB5:                                       ; preds = %NotNullBB3
  %arrayidx = getelementptr inbounds i32, i32* %1, i64 0
  %IsNull8 = icmp eq i32* %arrayidx, null
  br i1 %IsNull8, label %NullBB, label %NotNullBB7

NotNullBB7:                                       ; preds = %NotNullBB5
  store i32 100, i32* %arrayidx, align 4
  br label %label

label:                                            ; preds = %NotNullBB29, %NotNullBB7
  %IsNull10 = icmp eq i32** %ptr, null
  br i1 %IsNull10, label %NullBB, label %NotNullBB9

NotNullBB9:                                       ; preds = %label
  %2 = load i32*, i32** %ptr, align 8
  %cmp = icmp eq i32* %2, null
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %NotNullBB9
  %IsNull12 = icmp eq i32** %ptr, null
  br i1 %IsNull12, label %NullBB, label %NotNullBB11

NotNullBB11:                                      ; preds = %if.then
  %3 = load i32*, i32** %ptr, align 8
  %IsNull14 = icmp eq i32* %3, null
  br i1 %IsNull14, label %NullBB, label %NotNullBB13

NotNullBB13:                                      ; preds = %NotNullBB11
  %arrayidx1 = getelementptr inbounds i32, i32* %3, i64 0
  %IsNull16 = icmp eq i32* %arrayidx1, null
  br i1 %IsNull16, label %NullBB, label %NotNullBB15

NotNullBB15:                                      ; preds = %NotNullBB13
  store i32 100, i32* %arrayidx1, align 4
  br label %if.end

if.else:                                          ; preds = %NotNullBB9
  %IsNull18 = icmp eq i32** %ptr, null
  br i1 %IsNull18, label %NullBB, label %NotNullBB17

NotNullBB17:                                      ; preds = %if.else
  %4 = load i32*, i32** %ptr, align 8
  %IsNull20 = icmp eq i32* %4, null
  br i1 %IsNull20, label %NullBB, label %NotNullBB19

NotNullBB19:                                      ; preds = %NotNullBB17
  %arrayidx2 = getelementptr inbounds i32, i32* %4, i64 0
  %IsNull22 = icmp eq i32* %arrayidx2, null
  br i1 %IsNull22, label %NullBB, label %NotNullBB21

NotNullBB21:                                      ; preds = %NotNullBB19
  store i32 100, i32* %arrayidx2, align 4
  br label %if.end

if.end:                                           ; preds = %NotNullBB21, %NotNullBB15
  %IsNull24 = icmp eq i32** %ptr, null
  br i1 %IsNull24, label %NullBB, label %NotNullBB23

NotNullBB23:                                      ; preds = %if.end
  %5 = load i32*, i32** %ptr, align 8
  %IsNull26 = icmp eq i32* %5, null
  br i1 %IsNull26, label %NullBB, label %NotNullBB25

NotNullBB25:                                      ; preds = %NotNullBB23
  %arrayidx3 = getelementptr inbounds i32, i32* %5, i64 0
  %IsNull28 = icmp eq i32* %arrayidx3, null
  br i1 %IsNull28, label %NullBB, label %NotNullBB27

NotNullBB27:                                      ; preds = %NotNullBB25
  store i32 100, i32* %arrayidx3, align 4
  %IsNull30 = icmp eq i32** %arr.addr, null
  br i1 %IsNull30, label %NullBB, label %NotNullBB29

NotNullBB29:                                      ; preds = %NotNullBB27
  %6 = load i32*, i32** %arr.addr, align 8
  store i32* %6, i32** %ptr, align 8
  br label %label

NullBB:                                           ; preds = %NotNullBB27, %NotNullBB25, %NotNullBB23, %if.end, %NotNullBB19, %NotNullBB17, %if.else, %NotNullBB13, %NotNullBB11, %if.then, %label, %NotNullBB5, %NotNullBB3, %NotNullBB1, %NotNullBB, %entry
  %7 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([57 x i8], [57 x i8]* @0, i32 0, i32 0))
  ret void
}

declare dso_local i8* @mymalloc(i32) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %IsNull = icmp eq i32* %retval, null
  br i1 %IsNull, label %NullBB, label %NotNullBB

NotNullBB:                                        ; preds = %entry
  store i32 0, i32* %retval, align 4
  call void @foo(i32* null)
  ret i32 0

NullBB:                                           ; preds = %entry
  %0 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([58 x i8], [58 x i8]* @1, i32 0, i32 0))
  ret i32 0
}

declare i32 @printf(i8*, ...)

attributes #0 = { noinline nounwind optnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 10.0.0 (https://github.com/Systems-IIITD/CSE601.git 49d077240ba88639d805c42031ba63ca38f025b6)"}
