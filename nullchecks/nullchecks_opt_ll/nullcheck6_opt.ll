; ModuleID = 'nullcheck6.bc'
source_filename = "nullcheck6.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@0 = private unnamed_addr constant [57 x i8] c"error: null pointer dereference found in function `bar`\0A\00", align 1
@1 = private unnamed_addr constant [57 x i8] c"error: null pointer dereference found in function `foo`\0A\00", align 1
@2 = private unnamed_addr constant [58 x i8] c"error: null pointer dereference found in function `main`\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @bar(i32 %a) #0 {
entry:
  %a.addr = alloca i32, align 4
  %IsNull = icmp eq i32* %a.addr, null
  br i1 %IsNull, label %NullBB, label %NotNullBB

NotNullBB:                                        ; preds = %entry
  store i32 %a, i32* %a.addr, align 4
  %IsNull2 = icmp eq i32* %a.addr, null
  br i1 %IsNull2, label %NullBB, label %NotNullBB1

NotNullBB1:                                       ; preds = %NotNullBB
  %0 = load i32, i32* %a.addr, align 4
  ret i32 %0

NullBB:                                           ; preds = %NotNullBB, %entry
  %1 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([57 x i8], [57 x i8]* @0, i32 0, i32 0))
  ret i32 0
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @foo() #0 {
entry:
  %fnptr = alloca i32 (i32)*, align 8
  %IsNull = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull, label %NullBB, label %NotNullBB

NotNullBB:                                        ; preds = %entry
  store i32 (i32)* @bar, i32 (i32)** %fnptr, align 8
  %IsNull2 = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull2, label %NullBB, label %NotNullBB1

NotNullBB1:                                       ; preds = %NotNullBB
  %0 = load i32 (i32)*, i32 (i32)** %fnptr, align 8
  %IsNull4 = icmp eq i32 (i32)* %0, null
  br i1 %IsNull4, label %NullBB, label %NotNullBB3

NotNullBB3:                                       ; preds = %NotNullBB1
  %call = call i32 %0(i32 1)
  %1 = load i32 (i32)*, i32 (i32)** %fnptr, align 8
  %cmp = icmp ne i32 (i32)* %1, null
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %NotNullBB3
  %IsNull6 = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull6, label %NullBB, label %NotNullBB5

NotNullBB5:                                       ; preds = %if.then
  store i32 (i32)* null, i32 (i32)** %fnptr, align 8
  %IsNull8 = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull8, label %NullBB, label %NotNullBB7

NotNullBB7:                                       ; preds = %NotNullBB5
  %2 = load i32 (i32)*, i32 (i32)** %fnptr, align 8
  %IsNull10 = icmp eq i32 (i32)* %2, null
  br i1 %IsNull10, label %NullBB, label %NotNullBB9

NotNullBB9:                                       ; preds = %NotNullBB7
  %call1 = call i32 %2(i32 1)
  store i32 (i32)* @bar, i32 (i32)** %fnptr, align 8
  br label %if.end

if.else:                                          ; preds = %NotNullBB3
  %IsNull12 = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull12, label %NullBB, label %NotNullBB11

NotNullBB11:                                      ; preds = %if.else
  %3 = load i32 (i32)*, i32 (i32)** %fnptr, align 8
  %IsNull14 = icmp eq i32 (i32)* %3, null
  br i1 %IsNull14, label %NullBB, label %NotNullBB13

NotNullBB13:                                      ; preds = %NotNullBB11
  %call2 = call i32 %3(i32 1)
  br label %if.end

if.end:                                           ; preds = %NotNullBB13, %NotNullBB9
  %IsNull16 = icmp eq i32 (i32)** %fnptr, null
  br i1 %IsNull16, label %NullBB, label %NotNullBB15

NotNullBB15:                                      ; preds = %if.end
  %4 = load i32 (i32)*, i32 (i32)** %fnptr, align 8
  %IsNull18 = icmp eq i32 (i32)* %4, null
  br i1 %IsNull18, label %NullBB, label %NotNullBB17

NotNullBB17:                                      ; preds = %NotNullBB15
  %call3 = call i32 %4(i32 1)
  ret void

NullBB:                                           ; preds = %NotNullBB15, %if.end, %NotNullBB11, %if.else, %NotNullBB7, %NotNullBB5, %if.then, %NotNullBB1, %NotNullBB, %entry
  %5 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([57 x i8], [57 x i8]* @1, i32 0, i32 0))
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %IsNull = icmp eq i32* %retval, null
  br i1 %IsNull, label %NullBB, label %NotNullBB

NotNullBB:                                        ; preds = %entry
  store i32 0, i32* %retval, align 4
  call void @foo()
  ret i32 0

NullBB:                                           ; preds = %entry
  %0 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([58 x i8], [58 x i8]* @2, i32 0, i32 0))
  ret i32 0
}

declare i32 @printf(i8*, ...)

attributes #0 = { noinline nounwind optnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 10.0.0 (https://github.com/Systems-IIITD/CSE601.git 49d077240ba88639d805c42031ba63ca38f025b6)"}
