#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("knallbob");
MODULE_DESCRIPTION("Basic driver for dht22 device");

static char buffer[255];
static int buffer_pointer;
static u64 high_time[40] = {0};

static dev_t my_device_nr;
static struct class *my_class;
static struct cdev my_device;

u64 irq_time_high;
u64 irq_time_pre;
u64 irq_time_current;

unsigned int irq_number;

static int bit_ht[40] = {0};

static int temp = 0;
static int hum = 0;

static int irq_count = 0;
static const int    high = 1;
static const int    low  = 0;

#define DRIVER_NAME "dht22ydriver"
#define DRIVER_CLASS "firstdriverClass"

static void trigger_dht22(void){

	irq_count = 0;
	irq_time_pre = ktime_get_ns();
	gpio_direction_output(2, low);
	udelay(1000);

	gpio_direction_input(2);
}

static void process_results(void){
	
	temp = 0;
	hum = 0;
	
	int i = 0;
	for(i = 0; i < 40; i++){
		if(high_time[i] > 115000){
			bit_ht[i] = 1;
		}else{
			bit_ht[i] = 0;
		}
	}
	for(i = 0; i < 16; i++){
		hum *= 2;
		hum += bit_ht[i];
	}
	for(i = 16; i < 32; i++){
		temp *= 2;
		temp += bit_ht[i];
	}

	printk("Temperatur : %d \n", temp);
	printk("Humidity : %d \n", hum);
	
}

static irq_handler_t gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs) {
	
	irq_time_current = ktime_get_ns();
	irq_time_high = irq_time_current - irq_time_pre;

	if(irq_count - 3 >= 0 && irq_count - 3 < 40){
		high_time[irq_count - 3] = irq_time_high;
	}

	irq_count++;
	irq_time_pre = irq_time_current;
	return (irq_handler_t) IRQ_HANDLED; 
}

static ssize_t driver_read(struct file *File, char *user_buffer, size_t count, loff_t *offs) {
	int to_copy, not_copied, delta;

	to_copy = min(count, buffer_pointer);

	not_copied = copy_to_user(user_buffer, buffer, to_copy);

	delta = to_copy - not_copied;

	return delta;
}

static ssize_t driver_write(struct file *File, const char *user_buffer, size_t count, loff_t *offs) {
	int to_copy, not_copied, delta;
	char value;

	to_copy = min(count, sizeof(value));

	not_copied = copy_from_user(&value, user_buffer, to_copy);

	switch(value) {
		case '0':
			trigger_dht22();
			break;
		case '1':
			process_results();
			break;
		default:
			break;
	}

	delta = to_copy - not_copied;

	return delta;
}

static int driver_open(struct inode *device_file, struct file *instance) {
	printk("dev_nr - open was called!\n");
	return 0;
}

static int driver_close(struct inode *device_file, struct file *instance) {
	printk("dev_nr - close was called!\n");
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = driver_open,
	.release = driver_close,
	.read = driver_read,
	.write = driver_write
};

static int __init ModuleInit(void) {
	printk("qpio_irq: Loading module... ");
	int retval;

	/* Allocate a device nr */
	if( alloc_chrdev_region(&my_device_nr, 0, 1, DRIVER_NAME) < 0) {
		printk("Device Nr. could not be allocated!\n");
		return -1;
	}
	printk("read_write - Device Nr. Major: %d, Minor: %d was registered!\n", my_device_nr >> 20, my_device_nr && 0xfffff);

	/* Create device class */
	if((my_class = class_create(THIS_MODULE, DRIVER_CLASS)) == NULL) {
		printk("Device class can not e created!\n");
		goto ClassError;
	}

	/* create device file */
	if(device_create(my_class, NULL, my_device_nr, NULL, DRIVER_NAME) == NULL) {
		printk("Can not create device file!\n");
		goto FileError;
	}

	/* Initialize device file */
	cdev_init(&my_device, &fops);

	/* Regisering device to kernel */
	if(cdev_add(&my_device, my_device_nr, 1) == -1) {
		printk("Registering of device to kernel failed!\n");
		goto AddError;
	}

	/* Setup the gpio */
	if(gpio_request(2, "rpi-gpio-2")) {
		printk("Error!\nCan not allocate GPIO 2\n");
		return -1;
	}

	/* Set GPIO 17 direction */
	if(gpio_direction_input(2)) {
		printk("Error!\nCan not set GPIO 2 to input!\n");
		gpio_free(2);
		return -1;
	}

	/* Setup the interrupt */
	irq_number = gpio_to_irq(2);

	if(request_irq(irq_number, (irq_handler_t) gpio_irq_handler, IRQF_TRIGGER_RISING, "my_gpio_irq", NULL) != 0){
		printk("Error!\nCan not request interrupt nr.: %d\n", irq_number);
		gpio_free(2);
		return -1;
	}

	if(gpio_direction_output(2, high)) {
		printk("Error!\nCan not set GPIO 2 to output!\n");
		gpio_free(2);
		return -1;
	}

	printk("Done!\n");
	printk("GPIO 2 is mapped to IRQ Nr.: %d\n", irq_number);
	return 0;

AddError:
	device_destroy(my_class, my_device_nr);
FileError:
	class_destroy(my_class);
ClassError:
	unregister_chrdev_region(my_device_nr, 1);
	return -1;
}

static void __exit ModuleExit(void) {
	printk("gpio_irq: Unloading module... ");
	free_irq(irq_number, NULL);
	gpio_free(2);
	device_destroy(my_class, my_device_nr);
	class_destroy(my_class);
	unregister_chrdev_region(my_device_nr, 1);
}

module_init(ModuleInit);
module_exit(ModuleExit);