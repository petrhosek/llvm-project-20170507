// Test target codegen - host bc file has to be created first.
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=45 -fopenmp-cuda-mode -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm-bc %s -o %t-ppc-host.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=45 -fopenmp-cuda-mode -x c++ -triple nvptx64-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-ppc-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-64
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=45 -fopenmp-cuda-mode -x c++ -triple i386-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm-bc %s -o %t-x86-host.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=45 -fopenmp-cuda-mode -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=45 -fopenmp-cuda-mode -fexceptions -fcxx-exceptions -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// expected-no-diagnostics
#ifndef HEADER
#define HEADER

// Check that the execution mode of all 5 target regions on the gpu is set to SPMD Mode.
// CHECK-DAG: {{@__omp_offloading_.+l32}}_exec_mode = weak constant i8 0
// CHECK-DAG: {{@__omp_offloading_.+l38}}_exec_mode = weak constant i8 0
// CHECK-DAG: {{@__omp_offloading_.+l43}}_exec_mode = weak constant i8 0
// CHECK-DAG: {{@__omp_offloading_.+l48}}_exec_mode = weak constant i8 0
// CHECK-DAG: {{@__omp_offloading_.+l56}}_exec_mode = weak constant i8 0

#define N 1000
#define M 10

template<typename tx>
tx ftemplate(int n) {
  tx a[N];
  short aa[N];
  tx b[10];
  tx c[M][M];
  tx f = n;
  tx l;
  int k;
  tx *v;

#pragma omp target teams distribute parallel for lastprivate(l) dist_schedule(static,128) schedule(static,32)
  for(int i = 0; i < n; i++) {
    a[i] = 1;
    l = i;
  }

#pragma omp target teams distribute parallel for map(tofrom: aa) num_teams(M) thread_limit(64)
  for(int i = 0; i < n; i++) {
    aa[i] += 1;
  }

#pragma omp target teams distribute parallel for map(tofrom:a, aa, b) if(target: n>40) proc_bind(spread)
  for(int i = 0; i < 10; i++) {
    b[i] += 1;
  }

#pragma omp target teams distribute parallel for collapse(2) firstprivate(f) private(k)
  for(int i = 0; i < M; i++) {
    for(int j = 0; j < M; j++) {
      k = M;
      c[i][j] = i + j * f + k;
    }
  }

#pragma omp target teams distribute parallel for map(a, v[:N])
  for(int i = 0; i < n; i++)
    a[i] = v[i];
  return a[0];
}

int bar(int n){
  int a = 0;

  a += ftemplate<int>(n);

  return a;
}

// CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+}}_l32(
// CHECK-DAG: [[THREAD_LIMIT:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
// CHECK: call void @__kmpc_spmd_kernel_init(i32 [[THREAD_LIMIT]], i16 0, i16 0)
// CHECK: [[TEAM_ALLOC:%.+]] = call i8* @__kmpc_data_sharing_push_stack(i{{[0-9]+}} 4, i16 0)
// CHECK: [[BC:%.+]] = bitcast i8* [[TEAM_ALLOC]] to [[REC:%.+]]*
// CHECK: getelementptr inbounds [[REC]], [[REC]]* [[BC]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 91,
// CHECK: {{call|invoke}} void [[OUTL1:@.+]](
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: call void @__kmpc_spmd_kernel_deinit()
// CHECK: ret void

// CHECK: define internal void [[OUTL1]](
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 33,
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: ret void

// CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+}}(
// CHECK-DAG: [[THREAD_LIMIT:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
// CHECK: call void @__kmpc_spmd_kernel_init(i32 [[THREAD_LIMIT]], i16 0, i16 0)
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 91,
// CHECK: {{call|invoke}} void [[OUTL2:@.+]](
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: call void @__kmpc_spmd_kernel_deinit()
// CHECK: ret void

// CHECK: define internal void [[OUTL2]](
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 33,
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: ret void

// CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+}}(
// CHECK-DAG: [[THREAD_LIMIT:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
// CHECK: call void @__kmpc_spmd_kernel_init(i32 [[THREAD_LIMIT]], i16 0, i16 0)
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 91,
// CHECK: {{call|invoke}} void [[OUTL3:@.+]](
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: call void @__kmpc_spmd_kernel_deinit()
// CHECK: ret void

// CHECK: define internal void [[OUTL3]](
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 33,
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: ret void

// CHECK: define {{.*}}void {{@__omp_offloading_.+}}({{.+}}, i{{32|64}} [[F_IN:%.+]])
// CHECK: store {{.+}} [[F_IN]], {{.+}}* {{.+}},
// CHECK-DAG: [[THREAD_LIMIT:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
// CHECK: call void @__kmpc_spmd_kernel_init(i32 [[THREAD_LIMIT]], i16 0, i16 0)
// CHECK: store {{.+}} 99, {{.+}}* [[COMB_UB:%.+]], align
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 91, {{.+}}, {{.+}}, {{.+}}* [[COMB_UB]],
// CHECK: {{call|invoke}} void [[OUTL4:@.+]](
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: call void @__kmpc_spmd_kernel_deinit()
// CHECK: ret void

// CHECK: define internal void [[OUTL4]](
// CHECK: call void @__kmpc_for_static_init_4({{.+}}, {{.+}}, {{.+}} 33,
// CHECK: call void @__kmpc_for_static_fini(
// CHECK: ret void

// CHECK: define weak void @__omp_offloading_{{.*}}_l56(i[[SZ:64|32]] %{{[^,]+}}, [1000 x i32]* dereferenceable{{.*}}, i32* %{{[^)]+}})
// CHECK: call void [[OUTLINED:@__omp_outlined.*]](i32* %{{.+}}, i32* %{{.+}}, i[[SZ]] %{{.*}}, i[[SZ]] %{{.*}}, i[[SZ]] %{{.*}}, [1000 x i32]* %{{.*}}, i32* %{{.*}})
// CHECK: define internal void [[OUTLINED]](i32* noalias %{{.*}}, i32* noalias %{{.*}} i[[SZ]] %{{.+}}, i[[SZ]] %{{.+}}, i[[SZ]] %{{.+}}, [1000 x i32]* dereferenceable{{.*}}, i32* %{{.*}})

#endif
