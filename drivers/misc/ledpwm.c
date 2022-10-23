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

/* Globals */
static const unsigned long LED0_ADDRESS = 0xFF203080;
static const unsigned long LED7_ADDRESS = 0xFF20309C;
static const unsigned long address_size = LED7_ADDRESS - LED0_ADDRESS;
static const unsigned int led_on = 0x7ff;
static const unsigned int led_off; //0x000
static unsigned int dimming_values[8] = {0x400, 0x200, 0x100, 0x080,
	0x040, 0x020, 0x010, 0x008};
static void __iomem *io_addr; //0
static const size_t address_offset = 4;
static const size_t led_count = 8;
static unsigned int runled_counter; //0

/* Function prototypes */
static void timer_callback(struct timer_list *timer);
static DEFINE_TIMER(led_timer, timer_callback);

/* Functions */

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
		iowrite32(dimming_values[index],
			io_addr + (count * address_offset));
	}

	runled_counter++;

	/*
	 * Re-enable timer. Because this function will be called only
	 * first time. If we re-enable this will work like periodic timer.
	 */
	mod_timer(&led_timer, jiffies + msecs_to_jiffies(500));
} /* End timer_callback() */

/* This function is called on driver initialization */
static int __init led_init(void)
{
	unsigned long wait_time = msecs_to_jiffies(3000);
	unsigned int count = 0;

	/* Set name for requested memory */
	char *name = "LED DE1 Board";

	if (request_mem_region(LED0_ADDRESS, address_size, name) == 0)
		return -ENOMEM;

	/* Remapping from physical address to virtual address */
	io_addr = ioremap(LED0_ADDRESS, address_size);
	if (io_addr == 0) {
		/* When remapping gone wrong release memory region */
		release_mem_region(LED0_ADDRESS, address_size);
		return -1;
	}

	/* Light up all leds at full brightness */
	for (count = 0; count < led_count; count++)
		iowrite32(led_on, io_addr + (count * address_offset));

	/* Configure timer to start after 3seconds */
	mod_timer(&led_timer, jiffies + wait_time);

	printk(KERN_INFO "Load LED Driver!\n");
	return 0;
} /* End led_init() */

/* This function is called on driver exit */
static void __exit led_exit(void)
{
	unsigned int count = 0;

	/* Release timer */
	del_timer_sync(&led_timer);

	/* Turn off all leds */
	for (count = 0; count < led_count; count++)
		iowrite32(led_off, io_addr + (count * address_offset));

	/* Unmap virtual memory */
	iounmap(io_addr);

	/* Release requested memory region */
	release_mem_region(LED0_ADDRESS, address_size);

	printk(KERN_INFO "Deload LED Driver!\n");
} /* End led_exit() */

module_init(led_init);
module_exit(led_exit);

/* Module parameters */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A PWM Module for the DE1-Soc Board");
MODULE_AUTHOR("Alexander Prielinger <prielingeralexander@gmail.com>, Pascal Pletzer <S2010306018@fhooe.at>");
