// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A PWM Module for the DE1-Soc Board
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <asm/errno.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/stat.h>

/* Globals */
static const unsigned long LED0_ADDRESS = 0xFF203080;
static const unsigned long LED9_ADDRESS = 0xFF2030A4;
static const unsigned long address_size = 8;
static const unsigned int led_on = 0x7ff;
static const unsigned int led_off; //0x000
static unsigned int dimming_values[8] = { 0x400, 0x200, 0x100, 0x080,
					  0x040, 0x020, 0x010, 0x008 };
static u32 __iomem *io_addr; //0
static const size_t led_count = 8;
static unsigned int runled_counter; //0

struct led9 {
	struct cdev cdev;
	u32 *registers; // dummy
};

static struct led9 device_data;
static dev_t device_number;
static struct class *cdev_class;
static struct device *cdev_device;
static struct device *sysfs_device;

/* Function prototypes */
static void timer_callback(struct timer_list *timer);
static int led9_open(struct inode *inode, struct file *file);
static ssize_t led9_read(struct file *filep, char __user *buf, size_t count,
			 loff_t *offp);
static ssize_t led9_write(struct file *filep, const char __user *buf,
			  size_t count, loff_t *offp);
static ssize_t led9_off_show(struct device *dev, struct device_attribute *attr,
			     char *buf);

static DEFINE_TIMER(led_timer, timer_callback);
static DEVICE_ATTR_RO(led9_off);

/* Timer callback function for led lightning pattern */
static void timer_callback(struct timer_list *timer)
{
	unsigned int count = 0;
	unsigned int index = 0;

	/* Reset runled_counter */
	if (runled_counter == led_count)
		runled_counter = 0;

	/* Write lighting values to leds */
	for (count = 0; count < led_count; count++) {
		index = (runled_counter + count) % led_count;
		iowrite32(dimming_values[index], io_addr + count);
	}

	runled_counter++;

	/*
	 * Re-enable timer. Because this function will be called only
	 * first time. If we re-enable this will work like periodic timer.
	 */
	mod_timer(&led_timer, jiffies + msecs_to_jiffies(500));
} /* End timer_callback() */

/* Open character device function */
static int led9_open(struct inode *inode, struct file *file)
{
	struct led9 *data = container_of(inode->i_cdev, struct led9, cdev);

	file->private_data = data;

	printk(KERN_INFO "In %s\n", __func__);

	return 0;
} /* End led9_open() */

/* Read character device function */
static ssize_t led9_read(struct file *filep, char __user *buf, size_t count,
			 loff_t *offp)
{
	u32 led_value;
	u32 calcValue;
	int status;
	struct led9 *data = filep->private_data;

	/* End of file */
	if (*offp >= 4)
		return 0;

	printk(KERN_INFO "In %s. count: %d, off: %lld\n", __func__, count,
	       *offp);

	/* Small buffers */
	if (count < 4)
		return -ETOOSMALL;

	led_value = ioread32(data->registers);

	/* convert to percent */
	if (led_value >= 0 && led_value <= 2047)
		calcValue = led_value * 100 / 2047;

	printk(KERN_INFO "LED Percentage: %d%%\n", calcValue);

	status = copy_to_user(buf, &led_value, 1);
	if (status != 0)
		return -1;

	*offp += 4;
	return 4;
} /* End led9_read() */

/* Write to character device function */
static ssize_t led9_write(struct file *filep, const char __user *buf,
			  size_t count, loff_t *offp)
{
	u32 dimvalue = 0;
	u32 calcValue;
	int status;
	struct led9 *data = filep->private_data;

	/* Ignore last sent value (linefeed) */
	if (count > 1) {
		printk(KERN_INFO "In %s. count: %d, off: %lld\n", __func__,
		       count - 1, *offp);

		/*
		 * Only a valid percentage value from 0 to 100 is written
		 * Values beyond this region will be completely ignored
		 * 1 for one byte
		 */
		status = copy_from_user(&dimvalue, buf, 1);
		if (status != 0)
			return -1;

		if (dimvalue >= 0 && dimvalue <= 100) {
			calcValue = dimvalue * 2047 / 100;
			iowrite32(calcValue, data->registers);
			printk(KERN_INFO "Set LED to: %d%%\n", dimvalue);
		}

		/* Wait for 200ms */
		msleep(200);
	}
	return 1;
} /* End led9_write() */

/* struct for file operations */
static const struct file_operations fops = {
	.open = led9_open,
	.read = led9_read,
	.write = led9_write,
};

/* Sysfs entry */
static ssize_t led9_off_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	u32 value = 0;
	struct led9 *device_data = dev_get_drvdata(dev);

	printk(KERN_INFO "In %s\n", __func__);

	/* Read device data */
	if (ioread32(device_data->registers) != 0)
		value = 0;
	else
		value = 1;

	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
} /* End led9_off_show() */

/* This function is called on driver initialization */
static int __init led_init(void)
{
	unsigned long wait_time = msecs_to_jiffies(3000);
	unsigned int count = 0;
	int status;

	/* Set name for requested memory */
	char *name = "LED DE1 Board";

	/* Request memory addresses for LEDs */
	if (request_mem_region(LED0_ADDRESS, address_size, name) == 0)
		return -ENOMEM;

	/* Remapping from physical address to virtual address */
	io_addr = (u32 *)ioremap(LED0_ADDRESS, address_size);
	if (io_addr == 0) {
		/* When remapping gone wrong release memory region */
		release_mem_region(LED0_ADDRESS, address_size);
		return -1;
	}

	/* Light up all leds at full brightness */
	for (count = 0; count < led_count; count++)
		iowrite32(led_on, io_addr + count);

	/* Request memory for LED9 */
	if (request_mem_region(LED9_ADDRESS, 1, name) == 0)
		return -ENOMEM;

	/* Remap memory for LED9 */
	device_data.registers = ioremap(LED9_ADDRESS, 1);
	if (device_data.registers == 0) {
		/* When remapping gone wrong release memory region */
		release_mem_region(LED9_ADDRESS, 1);
		return -1;
	}

	/* Allocate character device */
	status = alloc_chrdev_region(&device_number, 0, 1, "led9");
	if (status < 0) {
		printk(KERN_INFO "Unable to allocate chardev region\n");
		goto exit;
	}

	/* Init character device structure */
	cdev_init(&device_data.cdev, &fops);
	device_data.cdev.owner = THIS_MODULE;

	/* Add character device */
	status = cdev_add(&device_data.cdev, device_number, 1);
	if (status < 0) {
		printk(KERN_INFO "Unable to add cdev\n");
		goto release_chardev;
	}

	/* Create device file */
	cdev_class = class_create(THIS_MODULE, "ldd5");
	if (IS_ERR(cdev_class)) {
		printk(KERN_INFO "Unable to create class\n");
		status = -EEXIST;
		goto remove_device;
	}

	cdev_device = device_create(cdev_class, NULL, device_number,
				    &device_data, "led9");
	if (IS_ERR(cdev_device)) {
		printk(KERN_INFO "Unable to create device\n");
		status = -EEXIST;
		goto remove_device_class;
	}

	/* Create sysfs file */
	sysfs_device = root_device_register("led9_off");
	dev_set_drvdata(sysfs_device, &device_data);
	status =
		sysfs_create_file(&sysfs_device->kobj, &dev_attr_led9_off.attr);
	if (status < 0) {
		printk(KERN_INFO "Unable to create file\n");
		return status;
	}

	/* Configure timer to start after 3seconds */
	mod_timer(&led_timer, jiffies + wait_time);

	/* Everything okay */
	printk(KERN_INFO "Load LED Driver!\n");
	return 0;

remove_device_class:
	class_destroy(cdev_class);
remove_device:
	cdev_del(&device_data.cdev);
release_chardev:
	unregister_chrdev_region(device_number, 1);
exit:
	return status;
} /* End led_init() */

/* This function is called on driver exit */
static void __exit led_exit(void)
{
	unsigned int count = 0;

	/* Release timer */
	del_timer_sync(&led_timer);

	/* Turn off all leds */
	for (count = 0; count < led_count; count++)
		iowrite32(led_off, io_addr + count);

	iowrite32(led_off, device_data.registers);

	/* Remove device file */
	device_destroy(cdev_class, device_number);
	class_destroy(cdev_class);

	/* Release resources */
	cdev_del(&device_data.cdev);
	unregister_chrdev_region(device_number, 1);

	/* Release sysfs */
	sysfs_remove_file(&sysfs_device->kobj, &dev_attr_led9_off.attr);
	root_device_unregister(sysfs_device);

	/* Unmap virtual memory */
	iounmap(io_addr);
	iounmap(device_data.registers);

	/* Release requested memory region */
	release_mem_region(LED0_ADDRESS, address_size);
	release_mem_region(LED9_ADDRESS, 1);

	printk(KERN_INFO "Deload LED Driver!\n");
} /* End led_exit() */

module_init(led_init);
module_exit(led_exit);

/* Module parameters */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A PWM Module for the DE1-Soc Board");
MODULE_AUTHOR(
	"Alexander Prielinger <prielingeralexander@gmail.com>, Pascal Pletzer <S2010306018@fhooe.at>");
