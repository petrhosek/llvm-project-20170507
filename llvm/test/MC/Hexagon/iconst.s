# RUN: llvm-mc -triple=hexagon -filetype=obj %s | llvm-objdump --print-imm-hex=false -d -r - | FileCheck %s

a:
# CHECK: r0 = add(r0, #0)
# CHECK: R_HEX_23_REG
r0 = iconst(#a)