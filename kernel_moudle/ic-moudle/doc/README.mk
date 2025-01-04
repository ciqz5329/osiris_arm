 添加用户刷新全部缓存的内核模块

```shell
sudo insmod ic_iallu.ko
```

卸载该内核模块

```
sudo rmmod ic_iallu
```

使用方法

在代码中添加

```cpp
    int fd = open("/dev/ic_iallu", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    // 测试未刷新缓存时的性能
    clock_t start = clock();
    test_function();
    clock_t end = clock();
    printf("Time before ic iallu: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    // 刷新指令缓存
    if (write(fd, "trigger", 7) < 0) {
        perror("Failed to write to device");
        close(fd);
        return 1;
    }
```

原理

加载模块后，内核会自动在 `/dev` 目录下创建设备文件 `/dev/ic_iallu`，通过写入设备文件触发 `ic iallu` 指令。
