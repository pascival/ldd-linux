// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A PWM Module for the DE1-Soc Board
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Globals */
struct ledpwm {
	struct miscdevice misc;
	u32 *registers;
};

/* Function prototypes */
static ssize_t ledpwm_read(struct file *filep, char __user *buf, size_t count,
			   loff_t *offp);
static ssize_t ledpwm_write(struct file *filep, const char __user *buf,
			    size_t count, loff_t *offp);
static int ledpwm_probe(struct platform_device *pdev);
static int ledpwm_remove(struct platform_device *pdev);

/* Struct for file operations */
static const struct file_operations ledpwm_fops = {
	.read = ledpwm_read,
	.write = ledpwm_write,
};

/* Description led driver */
static const struct of_device_id ledpwm_of_match[] = {
	{
		.compatible = "ldd,ledpwm",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ledpwm_of_match);

/* Struct led driver */
static struct platform_driver ledpwm_driver = {
	.driver = { .name = "led_driver",
		    .owner = THIS_MODULE,
		    .of_match_table = of_match_ptr(ledpwm_of_match) },
	.probe = ledpwm_probe,
	.remove = ledpwm_remove,
};
module_platform_driver(ledpwm_driver);

static int ledpwm_probe(struct platform_device *pdev)
{
	struct resource *io;
	int status = 0;
	char devName[10];
	static atomic_t ledpwm_no = ATOMIC_INIT(-1);
	int no = atomic_inc_return(&ledpwm_no);
	struct ledpwm *ledpwm =
		devm_kzalloc(&pdev->dev, sizeof(*ledpwm), GFP_KERNEL);
	platform_set_drvdata(pdev, ledpwm);

	/* Create device names */
	snprintf(devName, 10, "led%d", no);

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ledpwm->registers = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(ledpwm->registers))
		return PTR_ERR(ledpwm->registers);

	ledpwm->misc.name = devName;
	ledpwm->misc.minor = MISC_DYNAMIC_MINOR;
	ledpwm->misc.fops = &ledpwm_fops;
	ledpwm->misc.parent = &pdev->dev;
	status = misc_register(&ledpwm->misc);

	/* Return -1 on failure */
	if (status != 0)
		return -1;

	dev_info(ledpwm->misc.parent, "Devices created\n");
	return 0;
}

static int ledpwm_remove(struct platform_device *pdev)
{
	struct ledpwm *ledpwm = platform_get_drvdata(pdev);

	dev_info(ledpwm->misc.parent, "Destroying devices\n");

	misc_deregister(&ledpwm->misc);
	platform_set_drvdata(pdev, NULL);

	/* Reset LEDs */
	iowrite32(0, ledpwm->registers);

	return 0;
}

/* Read character device function */
static ssize_t ledpwm_read(struct file *filep, char __user *buf, size_t count,
			   loff_t *offp)
{
	u32 led_value = 0;
	u32 calcValue = 0;
	int status;

	struct ledpwm *ledpwm =
		container_of(filep->private_data, struct ledpwm, misc);

	/* End of file */
	if (*offp >= 1)
		return 0;

	dev_info(ledpwm->misc.parent, "In %s. count: %d, off: %lld\n", __func__,
		 count, *offp);

	/* Small buffers */
	if (count < 1)
		return -ETOOSMALL;

	led_value = ioread32(ledpwm->registers);

	/* Convert to percent */
	if (led_value >= 0 && led_value <= 2047)
		calcValue = led_value * 100 / 2047;

	dev_info(ledpwm->misc.parent, "LED Percentage: %d%%\n", calcValue);

	status = copy_to_user(buf, &led_value, 1);
	if (status != 0)
		return -1;

	*offp += 1;
	return 1;
} /* End ledpwm_read() */

/* Write to character device function */
static ssize_t ledpwm_write(struct file *filep, const char __user *buf,
			    size_t count, loff_t *offp)
{
	u32 dimvalue = 0;
	u32 calcValue;
	int status;
	struct ledpwm *ledpwm =
		container_of(filep->private_data, struct ledpwm, misc);

	dev_info(ledpwm->misc.parent, "In %s. count: %d, off: %lld\n", __func__,
		 count, *offp);

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
		iowrite32(calcValue, ledpwm->registers);
		dev_info(ledpwm->misc.parent, "Set LED to: %d%%\n", dimvalue);
	}

	/* Wait for 200ms */
	msleep(200);

	return 1;
} /* End ledpwm_write() */

/* Module parameters */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A PWM Module for the DE1-Soc Board");
MODULE_AUTHOR(
	"Alexander Prielinger <prielingeralexander@gmail.com>, Pascal Pletzer <S2010306018@fhooe.at>");
