define dso_local void @ssimulacra2_edge_diff_map_avx2(ptr noundef readonly captures(none) %0, ptr noundef readonly captures(none) %1, ptr noundef readonly captures(none) %2, ptr noundef readonly captures(none) %3, i32 noundef %4, i32 noundef %5, ptr noundef writeonly captures(none) %6) local_unnamed_addr {
  %8 = alloca [8 x float], align 32
  %9 = alloca [8 x float], align 32
  %10 = alloca [8 x float], align 32
  %11 = alloca [8 x float], align 32
  %12 = zext i32 %4 to i64
  %13 = zext i32 %5 to i64
  %14 = mul nuw i64 %13, %12
  %15 = uitofp i64 %14 to double
  %16 = fdiv double 1.000000e+00, %15
  %17 = icmp ult i64 %14, 8
  br label %19

18:                                               ; preds = %147
  ret void

19:                                               ; preds = %7, %147
  %20 = phi i64 [ 0, %7 ], [ %165, %147 ]
  %21 = mul i64 %14, %20
  %22 = getelementptr inbounds nuw float, ptr %0, i64 %21
  %23 = getelementptr inbounds nuw float, ptr %1, i64 %21
  %24 = getelementptr inbounds nuw float, ptr %2, i64 %21
  %25 = getelementptr inbounds nuw float, ptr %3, i64 %21
  br i1 %17, label %26, label %33

26:                                               ; preds = %48, %19
  %27 = phi double [ 0.000000e+00, %19 ], [ %89, %48 ]
  %28 = phi double [ 0.000000e+00, %19 ], [ %92, %48 ]
  %29 = phi double [ 0.000000e+00, %19 ], [ %93, %48 ]
  %30 = phi double [ 0.000000e+00, %19 ], [ %96, %48 ]
  %31 = phi i64 [ 0, %19 ], [ %34, %48 ]
  %32 = icmp ult i64 %31, %14
  br i1 %32, label %99, label %147

33:                                               ; preds = %19, %48
  %34 = phi i64 [ %49, %48 ], [ 8, %19 ]
  %35 = phi i64 [ %34, %48 ], [ 0, %19 ]
  %36 = phi double [ %96, %48 ], [ 0.000000e+00, %19 ]
  %37 = phi double [ %93, %48 ], [ 0.000000e+00, %19 ]
  %38 = phi double [ %92, %48 ], [ 0.000000e+00, %19 ]
  %39 = phi double [ %89, %48 ], [ 0.000000e+00, %19 ]
  %40 = getelementptr inbounds nuw float, ptr %22, i64 %35
  %41 = load <8 x float>, ptr %40, align 1
  %42 = getelementptr inbounds nuw float, ptr %24, i64 %35
  %43 = load <8 x float>, ptr %42, align 1
  %44 = getelementptr inbounds nuw float, ptr %23, i64 %35
  %45 = load <8 x float>, ptr %44, align 1
  %46 = getelementptr inbounds nuw float, ptr %25, i64 %35
  %47 = load <8 x float>, ptr %46, align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %8)
  call void @llvm.lifetime.start.p0(ptr nonnull %9)
  call void @llvm.lifetime.start.p0(ptr nonnull %10)
  call void @llvm.lifetime.start.p0(ptr nonnull %11)
  store <8 x float> %41, ptr %8, align 32
  store <8 x float> %45, ptr %9, align 32
  store <8 x float> %43, ptr %10, align 32
  store <8 x float> %47, ptr %11, align 32
  br label %51

48:                                               ; preds = %86
  call void @llvm.lifetime.end.p0(ptr nonnull %11)
  call void @llvm.lifetime.end.p0(ptr nonnull %10)
  call void @llvm.lifetime.end.p0(ptr nonnull %9)
  call void @llvm.lifetime.end.p0(ptr nonnull %8)
  %49 = add nuw i64 %34, 8
  %50 = icmp ugt i64 %49, %14
  br i1 %50, label %26, label %33, !llvm.loop !34

51:                                               ; preds = %33, %86
  %52 = phi i64 [ 0, %33 ], [ %97, %86 ]
  %53 = phi double [ %36, %33 ], [ %96, %86 ]
  %54 = phi double [ %37, %33 ], [ %93, %86 ]
  %55 = phi double [ %38, %33 ], [ %92, %86 ]
  %56 = phi double [ %39, %33 ], [ %89, %86 ]
  %57 = getelementptr inbounds nuw float, ptr %8, i64 %52
  %58 = load float, ptr %57, align 4
  %59 = getelementptr inbounds nuw float, ptr %9, i64 %52
  %60 = load float, ptr %59, align 4
  %61 = getelementptr inbounds nuw float, ptr %10, i64 %52
  %62 = load float, ptr %61, align 4
  %63 = getelementptr inbounds nuw float, ptr %11, i64 %52
  %64 = load float, ptr %63, align 4
  %65 = insertelement <2 x float> poison, float %62, i64 0
  %66 = insertelement <2 x float> %65, float %58, i64 1
  %67 = fpext <2 x float> %66 to <2 x double>
  %68 = insertelement <2 x float> poison, float %64, i64 0
  %69 = insertelement <2 x float> %68, float %60, i64 1
  %70 = fpext <2 x float> %69 to <2 x double>
  %71 = fsub <2 x double> %67, %70
  %72 = tail call <2 x double> @llvm.fabs.v2f64(<2 x double> %71)
  %73 = fadd <2 x double> %72, splat (double 1.000000e+00)
  %74 = shufflevector <2 x double> %73, <2 x double> poison, <2 x i32> <i32 1, i32 poison>
  %75 = fdiv <2 x double> %73, %74
  %76 = extractelement <2 x double> %75, i64 0
  %77 = fadd double %76, -1.000000e+00
  %78 = tail call double @llvm.fabs.f64(double %77)
  %79 = fcmp ueq double %78, 0x7FF0000000000000
  br i1 %79, label %86, label %80

80:                                               ; preds = %51
  %81 = fcmp ogt double %77, 0.000000e+00
  %82 = select i1 %81, double %77, double 0.000000e+00
  %83 = fcmp olt double %77, 0.000000e+00
  %84 = fneg double %77
  %85 = select i1 %83, double %84, double 0.000000e+00
  br label %86

86:                                               ; preds = %51, %80
  %87 = phi double [ %82, %80 ], [ %77, %51 ]
  %88 = phi double [ %85, %80 ], [ %77, %51 ]
  %89 = fadd double %56, %87
  %90 = fmul double %87, %87
  %91 = fmul double %90, %90
  %92 = fadd double %55, %91
  %93 = fadd double %54, %88
  %94 = fmul double %88, %88
  %95 = fmul double %94, %94
  %96 = fadd double %53, %95
  %97 = add nuw nsw i64 %52, 1
  %98 = icmp eq i64 %97, 8
  br i1 %98, label %48, label %51, !llvm.loop !35

99:                                               ; preds = %26, %134
  %100 = phi i64 [ %145, %134 ], [ %31, %26 ]
  %101 = phi double [ %144, %134 ], [ %30, %26 ]
  %102 = phi double [ %141, %134 ], [ %29, %26 ]
  %103 = phi double [ %140, %134 ], [ %28, %26 ]
  %104 = phi double [ %137, %134 ], [ %27, %26 ]
  %105 = getelementptr inbounds nuw float, ptr %22, i64 %100
  %106 = load float, ptr %105, align 4
  %107 = getelementptr inbounds nuw float, ptr %23, i64 %100
  %108 = load float, ptr %107, align 4
  %109 = getelementptr inbounds nuw float, ptr %24, i64 %100
  %110 = load float, ptr %109, align 4
  %111 = getelementptr inbounds nuw float, ptr %25, i64 %100
  %112 = load float, ptr %111, align 4
  %113 = insertelement <2 x float> poison, float %110, i64 0
  %114 = insertelement <2 x float> %113, float %106, i64 1
  %115 = fpext <2 x float> %114 to <2 x double>
  %116 = insertelement <2 x float> poison, float %112, i64 0
  %117 = insertelement <2 x float> %116, float %108, i64 1
  %118 = fpext <2 x float> %117 to <2 x double>
  %119 = fsub <2 x double> %115, %118
  %120 = tail call <2 x double> @llvm.fabs.v2f64(<2 x double> %119)
  %121 = fadd <2 x double> %120, splat (double 1.000000e+00)
  %122 = shufflevector <2 x double> %121, <2 x double> poison, <2 x i32> <i32 1, i32 poison>
  %123 = fdiv <2 x double> %121, %122
  %124 = extractelement <2 x double> %123, i64 0
  %125 = fadd double %124, -1.000000e+00
  %126 = tail call double @llvm.fabs.f64(double %125)
  %127 = fcmp ueq double %126, 0x7FF0000000000000
  br i1 %127, label %134, label %128

128:                                              ; preds = %99
  %129 = fcmp ogt double %125, 0.000000e+00
  %130 = select i1 %129, double %125, double 0.000000e+00
  %131 = fcmp olt double %125, 0.000000e+00
  %132 = fneg double %125
  %133 = select i1 %131, double %132, double 0.000000e+00
  br label %134

134:                                              ; preds = %99, %128
  %135 = phi double [ %130, %128 ], [ %125, %99 ]
  %136 = phi double [ %133, %128 ], [ %125, %99 ]
  %137 = fadd double %104, %135
  %138 = fmul double %135, %135
  %139 = fmul double %138, %138
  %140 = fadd double %103, %139
  %141 = fadd double %102, %136
  %142 = fmul double %136, %136
  %143 = fmul double %142, %142
  %144 = fadd double %101, %143
  %145 = add nuw i64 %100, 1
  %146 = icmp eq i64 %145, %14
  br i1 %146, label %147, label %99, !llvm.loop !36

147:                                              ; preds = %134, %26
  %148 = phi double [ %27, %26 ], [ %137, %134 ]
  %149 = phi double [ %28, %26 ], [ %140, %134 ]
  %150 = phi double [ %29, %26 ], [ %141, %134 ]
  %151 = phi double [ %30, %26 ], [ %144, %134 ]
  %152 = fmul double %16, %148
  %153 = shl nuw nsw i64 %20, 5
  %154 = getelementptr inbounds nuw i8, ptr %6, i64 %153
  store double %152, ptr %154, align 8
  %155 = fmul double %16, %149
  %156 = tail call double @sqrt(double noundef %155)
  %157 = tail call double @sqrt(double noundef %156)
  %158 = getelementptr inbounds nuw i8, ptr %154, i64 8
  store double %157, ptr %158, align 8
  %159 = fmul double %16, %150
  %160 = getelementptr inbounds nuw i8, ptr %154, i64 16
  store double %159, ptr %160, align 8
  %161 = fmul double %16, %151
  %162 = tail call double @sqrt(double noundef %161)
  %163 = tail call double @sqrt(double noundef %162)
  %164 = getelementptr inbounds nuw i8, ptr %154, i64 24
  store double %163, ptr %164, align 8
  %165 = add nuw nsw i64 %20, 1
  %166 = icmp eq i64 %165, 3
  br i1 %166, label %18, label %19, !llvm.loop !37
}
