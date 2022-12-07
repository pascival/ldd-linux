// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PushButton Module for DE1-SoC
 * synchronisation and interrupts
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

/* Globals */
#define FIFO_SIZE 8
struct device_data {
	u32 *registers[2];
	struct miscdevice misc;
	struct kfifo fifo;
	struct mutex mutex;
	spinlock_t lock;
	wait_queue_head_t queue;
};

/* Function prototypes */
static int key_probe(struct platform_device *pdev);
static int key_remove(struct platform_device *pdev);
static irqreturn_t handler(int irq, void *data);
static ssize_t key_open(struct inode *inode, struct file *filep);
static ssize_t key_release(struct inode *inode, struct file *filep);
static ssize_t key_read(struct file *filep, char __user *buf, size_t count,
			loff_t *offp);

/* File operation struct */
static const struct file_operations key_fops = {
	.open = key_open,
	.release = key_release,
	.read = key_read,
};

/* Description key driver */
static const struct of_device_id key_of_match[] = {
	{
		.compatible = "ldd,pushbutton",
	},
	{},
};
MODULE_DEVICE_TABLE(of, key_of_match);

/* Struct key driver */
static struct platform_driver key_driver = {
	.driver = { .name = "key_driver",
		    .owner = THIS_MODULE,
		    .of_match_table = of_match_ptr(key_of_match) },
	.probe = key_probe,
	.remove = key_remove,
};
module_platform_driver(key_driver);

/* Key probe function */
static int key_probe(struct platform_device *pdev)
{
	struct resource *irq_reg;
	struct resource *edge_reg;
	int status = 0;
	int irq = 0;
	struct device_data *key =
		devm_kzalloc(&pdev->dev, sizeof(*key), GFP_KERNEL);
	if (kfifo_alloc(&key->fifo, FIFO_SIZE, GFP_KERNEL))
		return -ENOMEM;

	platform_set_drvdata(pdev, key);

	irq_reg = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	edge_reg = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	key->registers[0] = devm_ioremap_resource(&pdev->dev, irq_reg);
	key->registers[1] = devm_ioremap_resource(&pdev->dev, edge_reg);
	key->misc.name = "key";
	key->misc.minor = MISC_DYNAMIC_MINOR;
	key->misc.fops = &key_fops;
	key->misc.parent = &pdev->dev;

	status = misc_register(&key->misc);
	if (status != 0)
		return -ENODEV;

	/* Initialize interrupt */
	irq = platform_get_irq(pdev, 0);
	status = devm_request_irq(&pdev->dev, irq, handler, IRQF_SHARED,
				  dev_name(&pdev->dev), key);
	if (status)
		return -1;

	/* Activate Interrupt */
	iowrite32(0xF, key->registers[1]);
	iowrite32(0xF, key->registers[0]);

	/* Initializing synchronisation */
	mutex_init(&key->mutex);
	spin_lock_init(&key->lock);
	init_waitqueue_head(&key->queue);

	dev_info(key->misc.parent, "Devices created\n");
	return 0;
} /* End key_probe() */

/* Key remove function */
static int key_remove(struct platform_device *pdev)
{
	struct device_data *key = platform_get_drvdata(pdev);

	dev_info(key->misc.parent, "Destroying devices\n");

	/* Deactivate Interrupt */
	iowrite32(0, key->registers[0]);

	/* Release kfifo */
	kfifo_free(&key->fifo);
	// mutex_destroy

	misc_deregister(&key->misc);
	platform_set_drvdata(pdev, NULL);

	return 0;
} /* End key_remove() */

/* Interrupt handler function */
static irqreturn_t handler(int irq, void *data)
{
	struct device_data *key = data;
	u32 var = ioread32(key->registers[1]);

	dev_info(key->misc.parent, "IRQ BTN: %d", var);

	/* Store button in kfifo */
	kfifo_in_spinlocked(&key->fifo, &var, 1, &key->lock);

	/* Reset egde register */
	iowrite32(0xF, key->registers[1]);

	/* Signal waitqueue for event */
	wake_up_interruptible(&key->queue);

	return IRQ_HANDLED;
} /* End handler() */

/* Open character device function */
static ssize_t key_open(struct inode *inode, struct file *filep)
{
	struct device_data *key =
		container_of(filep->private_data, struct device_data, misc);

	/* Mutex lock for character device */
	int status = mutex_lock_interruptible(&key->mutex);

	dev_info(key->misc.parent, "mutex claimed");
	return status;
} /* End key_open() */

/* Release character device function */
static ssize_t key_release(struct inode *inode, struct file *filep)
{
	struct device_data *key =
		container_of(filep->private_data, struct device_data, misc);

	/* Unlock mutex for character device */
	mutex_unlock(&key->mutex);
	dev_info(key->misc.parent, "mutex released: %d",
		 key->mutex.owner.counter);
	return 0;
} /* End key_release() */

/* Read character device function */
static ssize_t key_read(struct file *filep, char __user *buf, size_t count,
			loff_t *offp)
{
	u_char button_buff[FIFO_SIZE];
	u32 fifo_size = 0;
	int status;
	int i = 0;
	struct device_data *key =
		container_of(filep->private_data, struct device_data, misc);

	/* End of file */
	if (*offp >= 1)
		return 0;

	/* Small buffers */
	if (count < 1)
		return -ETOOSMALL;

	/* Wait for event if buffer is empty */
	wait_event_interruptible(key->queue, kfifo_len(&key->fifo) != 0);
	//returnt beu unterbrechung ungleich 0 -> fall behandeln

	/* Read kfifo */
	fifo_size = kfifo_out_spinlocked(&key->fifo, button_buff, FIFO_SIZE,
					 &key->lock);
	dev_info(key->misc.parent, "FIFO SIZE: %d\n", fifo_size);
	for (i = 0; i < fifo_size; i++)
		dev_info(key->misc.parent, "Buffer[%d]: %d\n", i,
			 button_buff[i]);

	/* Write kfifo stream to character device */
	status = copy_to_user(buf, button_buff, fifo_size);
	if (status != 0)
		return -1;

	*offp += fifo_size;
	return fifo_size;
} /* End key_read() */

/* Module parameters */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PushButton Module for DE1-SoC");
MODULE_AUTHOR(
	"Alexander Prielinger <prielingeralexander@gmail.com>, Pascal Pletzer <S2010306018@fhooe.at>");
