#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
    pr_info("hello_kmod: loaded\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("hello_kmod: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borys");
MODULE_DESCRIPTION("Test kernel module");