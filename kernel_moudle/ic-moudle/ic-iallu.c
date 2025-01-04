#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define DEVICE_NAME "ic_iallu"

static int major_number;
static struct class *ic_iallu_class = NULL;
static struct device *ic_iallu_device = NULL;
static struct cdev ic_iallu_cdev;

static ssize_t ic_iallu_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    // 执行 ic iallu 指令
    asm volatile("ic iallu" : : : "memory");

    printk(KERN_INFO "ic iallu instruction executed\n");

    return count;
}

static struct file_operations fops = {
    .write = ic_iallu_write,
};

static int __init ic_iallu_init(void)
{
    dev_t dev;

    // 分配主设备号
    if (alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_ERR "Failed to allocate chrdev region\n");
        return -1;
    }

    major_number = MAJOR(dev);

    // 创建设备类
    ic_iallu_class = class_create(DEVICE_NAME);
    if (IS_ERR(ic_iallu_class)) {
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "Failed to create device class\n");
        return PTR_ERR(ic_iallu_class);
    }

    // 创建设备文件
    ic_iallu_device = device_create(ic_iallu_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(ic_iallu_device)) {
        class_destroy(ic_iallu_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "Failed to create device\n");
        return PTR_ERR(ic_iallu_device);
    }

    // 初始化字符设备
    cdev_init(&ic_iallu_cdev, &fops);
    if (cdev_add(&ic_iallu_cdev, dev, 1) < 0) {
        device_destroy(ic_iallu_class, dev);
        class_destroy(ic_iallu_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "Failed to add cdev\n");
        return -1;
    }

    printk(KERN_INFO "ic_iallu module loaded with major number %d\n", major_number);

    return 0;
}

static void __exit ic_iallu_exit(void)
{
    dev_t dev = MKDEV(major_number, 0);

    cdev_del(&ic_iallu_cdev);
    device_destroy(ic_iallu_class, dev);
    class_destroy(ic_iallu_class);
    unregister_chrdev_region(dev, 1);

    printk(KERN_INFO "ic_iallu module unloaded\n");
}

module_init(ic_iallu_init);
module_exit(ic_iallu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Kernel module to execute ic iallu instruction");