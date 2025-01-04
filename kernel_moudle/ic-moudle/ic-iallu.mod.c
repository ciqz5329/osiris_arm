#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const char ____versions[]
__used __section("__versions") =
	"\x10\x00\x00\x00\x7e\x3a\x2c\x12"
	"_printk\0"
	"\x1c\x00\x00\x00\x2b\x2f\xec\xe3"
	"alloc_chrdev_region\0"
	"\x18\x00\x00\x00\x04\x93\xd6\xa1"
	"class_create\0\0\0\0"
	"\x18\x00\x00\x00\xfc\x19\x79\x64"
	"device_create\0\0\0"
	"\x14\x00\x00\x00\xff\x12\x90\x7e"
	"cdev_init\0\0\0"
	"\x14\x00\x00\x00\xa3\x1d\x61\xe0"
	"cdev_add\0\0\0\0"
	"\x24\x00\x00\x00\x33\xb3\x91\x60"
	"unregister_chrdev_region\0\0\0\0"
	"\x18\x00\x00\x00\x2b\x1e\xf2\x33"
	"class_destroy\0\0\0"
	"\x18\x00\x00\x00\x90\x82\x8d\xc2"
	"device_destroy\0\0"
	"\x1c\x00\x00\x00\xcb\xf6\xfd\xf0"
	"__stack_chk_fail\0\0\0\0"
	"\x14\x00\x00\x00\xf9\x14\x7b\x9c"
	"cdev_del\0\0\0\0"
	"\x18\x00\x00\x00\xa9\x2b\x1c\x58"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "D68BE089B12B65E4C5A7D2B");
