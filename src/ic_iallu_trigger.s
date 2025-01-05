.data
device_path: .asciz "/dev/ic_iallu"  // 设备文件路径
buffer:      .asciz "trigger"        // 写入的数据
buffer_len = . - buffer              // 数据长度

.text
.global trigger_ic_iallu             // 导出函数
trigger_ic_iallu:
    // 保存寄存器
    stp x29, x30, [sp, #-16]!        // 保存 x29 (FP) 和 x30 (LR)
    stp x19, x20, [sp, #-16]!        // 保存 x19 和 x20

    // 打开设备文件
    mov x0, #-100           // AT_FDCWD (当前工作目录)
    ldr x1, =device_path    // 设备文件路径
    mov x2, #1              // O_WRONLY (只写模式)
    mov x8, #56             // open 系统调用号
    svc #0                  // 调用 open
    cmp x0, #0              // 检查返回值
    b.lt exit               // 如果返回值 < 0，跳转到 exit

    // 保存文件描述符
    mov x19, x0

    // 写入数据
    mov x0, x19             // 文件描述符
    ldr x1, =buffer         // 写入的数据
    ldr x2, =buffer_len     // 数据长度
    mov x8, #64             // write 系统调用号
    svc #0                  // 调用 write
    cmp x0, #0              // 检查返回值
    b.lt close_file         // 如果返回值 < 0，跳转到 close_file

close_file:
    // 关闭设备文件
    mov x0, x19             // 文件描述符
    mov x8, #57             // close 系统调用号
    svc #0                  // 调用 close

exit:
    // 恢复寄存器
    ldp x19, x20, [sp], #16  // 恢复 x19 和 x20
    ldp x29, x30, [sp], #16  // 恢复 x29 (FP) 和 x30 (LR)

    ret                     // 返回