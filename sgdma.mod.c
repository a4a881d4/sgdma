#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x9a31bb74, "module_layout" },
	{ 0x754d539c, "strlen" },
	{ 0xc29bf967, "strspn" },
	{ 0x86db9a2d, "pci_disable_device" },
	{ 0xbafecf06, "remove_proc_entry" },
	{ 0x6efb8d26, "x86_dma_fallback_dev" },
	{ 0x91715312, "sprintf" },
	{ 0x4f8b5ddb, "_copy_to_user" },
	{ 0xd23f1d25, "proc_mkdir" },
	{ 0xe364ba6d, "pci_iounmap" },
	{ 0x27e1a049, "printk" },
	{ 0x20c55ae0, "sscanf" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x5a921311, "strncmp" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xa202a8e5, "kmalloc_order_trace" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xf55bb238, "pci_unregister_driver" },
	{ 0x26034a58, "proc_create_data" },
	{ 0x37a0cba, "kfree" },
	{ 0xaba33759, "__pci_register_driver" },
	{ 0x827937f7, "pci_iomap" },
	{ 0x436c2179, "iowrite32" },
	{ 0xace5f3c3, "pci_enable_device" },
	{ 0x4f6b400b, "_copy_from_user" },
	{ 0xc6849b4a, "dma_ops" },
	{ 0xe484e35f, "ioread32" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v00001172d00000004sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "A4268801734CA2FEC49EE9F");
