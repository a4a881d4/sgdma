/*======================================================================*/
/* Project:     Implementation PCIe8x board (EP4S40G5HI2)               */
/* Title:       driver for Stratix IV PCIe Chaning DMA                  */
/* Purpose:     - driver module that enables to read/write registers    */
/*              of the card also from user space application            */
/*              mapping DMA descriptor table to user space and          */
/*              start DMA operation by user space application           */
/*              this driver is only for study and test                  */
/*              altera PCIe example                                     */
/*                                                                      */
/*                                                                      */
/* File:        usgdma.c                                                */
/* Version:     0.1                                                     */
/* Date:        09/2013                                                 */
/*                                                                      */
/* Description: - wake-up the device, allocate and map its memory,      */
/*              access io by proc file                                  */
/*                                                                      */
/* Software:    Linux Debian kernel 3.8.0-19                            */
/* Hardware:    WCK card                                                */
/*                                                                      */
/* Author:      a4a881d4                                                */
/*                                                                      */
/* Note: This code is based on the code from the book                   */
/*       "Linux Device Drivers, Third Edition" by                       */
/*       Alessandro Rubini, Jonathan Corbet and                         */
/*       Greg Kroah-Hartman, published by O'Reilly & Associates.        */
/*                                                                      */
/*       No warranty is attached, we cannot take responsibility         */
/*       for errors or fitness for use.                                 */
/*======================================================================*/

/**
 * includes 
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include <linux/proc_fs.h>	/* Necessary because we use the proc fs */

#define procfs_name "usg"
struct proc_dir_entry *usg_Proc_File;

#include "usgdma.h"


MODULE_DESCRIPTION("usg");
MODULE_AUTHOR("a4a881d4");
MODULE_LICENSE("GPL");

/**
 * Prototypes for shared functions
 */
static void usg_remove( struct pci_dev *dev );
static int usg_probe( struct pci_dev *dev, const struct pci_device_id *id );

/**
 * module parameters that can be passed in
 */

module_param(usg_dma_buffer_size, int, S_IRUGO);

/**
 * define devices our driver supports
 */
static struct pci_device_id usg_ids[] = {
	{	PCI_VENDOR_ID_USGDMA,	/*  Interface chip manufacturer     ID */
		0x0004,           	/*  Device ID for the ??? function */
		PCI_ANY_ID,			/*  Subvendor ID wild card */
		PCI_ANY_ID,         /*  Subdevice ID wild card */
		0, 0,               /*  class and classmask are unspecified */
		0					/*  Use this to co-relate configuration information if the driver   */  
		/*supports multiple cards. Can be an enumerated type. */
	}, {0}
};

/**
 * export pci_device_id structure to user space, allowing hotplug
 * and informing the module loading system about what module works
 * with what hardware device
 */
MODULE_DEVICE_TABLE (pci, usg_ids);

/**
 * create pci_driver structure,
 * and register it in usg_driver_init_module,
 * unregister in usg_driver_exit_module
 */
static struct pci_driver usg_driver = {
	.name     = USGDMA_DRIVER_NAME,
	.id_table = usg_ids,
	.probe    = usg_probe,
	.remove   = usg_remove,
};

/**
 * internal structure
 * info on our usg card
 */ 
struct usg_struct {
	void __iomem *base_address;	/* address in kernel space (virtual memory)*/
	unsigned long mmio_base;		/* physical address */
	unsigned long mmio_len;			/* size/length of the memory */
	struct cdev cdev;						/* char device structure */
};

static struct usg_struct usg_card_struct;
static struct usg_struct *usg_card = &usg_card_struct;

/**
 * file operations structure
 * (file of operations that an application can invoke on the device)
 */
struct file_operations usg_fops = {
	.owner =	THIS_MODULE,
	.open =		usg_open,
	.read =		usg_read,
	.write =	usg_write,
	.release =	usg_release,
};

/**
 * open function
 * 
 * get the pointer to the usg_struct and store it in the 
 * private_data space
 */
int usg_open(struct inode *inode, struct file *filp)
{
  struct usg_struct *dev; // device information

  dev = container_of(inode->i_cdev, struct usg_struct, cdev);
  filp->private_data = dev; // store for other methods 

  printk(KERN_DEBUG "opening dev\n");
  return 0;
}

/**
 * release function
 * 
 * close device, most of the cleaning stuff is made in the
 * function usg_remove
 */
int usg_release(struct inode *inode, struct file *filp)
{
    printk(KERN_DEBUG "closing...\n");
	return 0;
}

/**
 * read function
 */
ssize_t usg_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)//f_pos 是这次对文件进行操作的起始位置
{
	return count;
}

/**
 * write function
 */
ssize_t usg_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct usg_struct *dev = filp->private_data;
	return count;
}

/**
 * Set up the char_dev structure for this device.
 */
static int usg_setup_cdev(struct usg_struct *dev)
{
  int err = 0; 
  dev_t devno = MKDEV(usg_major, usg_minor);

  cdev_init(&dev->cdev, &usg_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &usg_fops;
  /* note: do not call cdev_add until the driver is completly ready 
  to handle operations on the device */
  err = cdev_add (&dev->cdev, devno, 1); 
  /* Fail gracefully if need be */
  if (err)
    printk(KERN_WARNING "Error %d adding usg", err);
  
  return err;
}

/**
 * Get major and minor number to work with, asking for a dynamic
 * major unless directed otherwise at load or compile time.
 */
int allocate_device_number(void)
{
  int result = 0;
  dev_t dev = 0;
  
  if (usg_major) {   //if major number defined
    dev = MKDEV(usg_major, usg_minor);
    result = register_chrdev_region(dev, 
                                    usg_nr_devs, USGDMA_DRIVER_NAME);
  } else { //if major num not defined (equals 0)- allocate dynamically
    result = alloc_chrdev_region(&dev, usg_minor, 
                                  usg_nr_devs, USGDMA_DRIVER_NAME);
    usg_major = MAJOR(dev);
  }
  if (result < 0) {
    printk(KERN_WARNING "usg_driver: can't get major %d\n",
           usg_major);
    return result;
  }
  printk(KERN_DEBUG "usg_driver: major number is %d\n", 
         usg_major);
  
  return result;
}

/**
 * This function is called by the PCI core when 
 * it has a struct pci_dev that it thinks this driver wants to control.
 * 
 * Purpose: initialize the device properly 
 */
static int usg_probe(struct pci_dev *dev, 
                                      const struct pci_device_id *id)
{
	int ret_val = 0;
	u16 command;

	//initialization of physical mmio_base and mmio_len
	usg_card->mmio_base = pci_resource_start(dev,USGDMA_BAR);
	usg_card->mmio_len = pci_resource_len(dev,USGDMA_BAR);
	usg_card->base_address = pci_iomap(dev, USGDMA_BAR, usg_card->mmio_len);

	printk(KERN_DEBUG "usgdma card found!\n");
	printk(KERN_DEBUG "BAR1 Start Address: %lx\n", usg_card->mmio_base);
	printk(KERN_DEBUG "BAR1 Mem Len:%lu\n", usg_card->mmio_len);


	//wake up the device
	ret_val = pci_enable_device(dev);
	if(ret_val!=0){
		printk(KERN_WARNING "function pci_enable_device failed\n");
		return 1;
	}


	// setup cdev structure 
	// afterwards the kernel can access the operations on the device
	ret_val = usg_setup_cdev(usg_card);
	if(ret_val != 0) {
		printk(KERN_WARNING "cannot add device to the system!\n");
		return 1;
	}

	return 0;
}

/**
 * function to be executed when unloading the driver module
 */
static void usg_remove(struct pci_dev *dev)
{
	printk(KERN_DEBUG "usg_driver removing...\n" ); 

	//remove char device from the system
	cdev_del(&usg_card->cdev);

	//release virtual memory
	iounmap(usg_card->base_address);
	pci_disable_device(dev);
}

static size_t
procfile_read(char *buffer,
	      char **buffer_location,
	      off_t offset, int buffer_length, int *eof, void *data)
{
	int ret;
	int len;
	
	printk(KERN_INFO "procfile_read (/proc/%s len=%d off=%d) called\n", procfs_name, buffer_length,offset);
	
	len = buffer_length;
	
	if( offset+buffer_length > 8192 ) 
	{
		*eof=1;
		len=8192-offset;
	}
	memcpy( buffer, usg_card->base_address+offset, len );
	
	printk(KERN_INFO "first DW = %08x\n", *(int*)buffer);
	
	return len;

}

static ssize_t procfile_write( struct file *filp, const char __user *buff, unsigned long len, void *data )
{
    int capacity = 8192;
    if (len > capacity)
    {
        printk(KERN_INFO "No space to write in procEntry123!\n");
        return -1;
    }
		printk(KERN_INFO "procfile_write (/proc/%s len=%d) called\n", procfs_name, len);
    if (copy_from_user( usg_card->base_address, buff, len ))
    {
        return -2;
    }

    return len;
}
/**
 * init module function
 */
static int __init usg_init_module(void)
{
	int ret_val = 0;
	printk( KERN_DEBUG "Module usg_driver init\n" );
	ret_val = allocate_device_number();
	ret_val = pci_register_driver(&usg_driver);
	
	usg_Proc_File = create_proc_entry(procfs_name, 0666, NULL);
	
	if (usg_Proc_File == NULL) {
		remove_proc_entry(procfs_name, NULL);
		printk(KERN_ALERT "Error: Could not initialize /proc/%s\n",
		       procfs_name);
		return -ENOMEM;
	}

	usg_Proc_File->read_proc = procfile_read;
	usg_Proc_File->write_proc = procfile_write;
	usg_Proc_File->size 	 = 8192;

	printk(KERN_INFO "/proc/%s created\n", procfs_name);	
	
	return ret_val;
}

/**
 * exit module function
 */
 
static void __exit usg_exit_module(void)
{
	dev_t dev_no = MKDEV(usg_major, usg_minor);

	printk( KERN_DEBUG "Module usg_driver exit\n" );

	/*after unregistering all PCI devices bound to this driver
	will be removed*/
	pci_unregister_driver(&usg_driver);
	//deallocate device numbers
	unregister_chrdev_region(dev_no, usg_nr_devs);
	
	remove_proc_entry(procfs_name, NULL);
	printk(KERN_INFO "/proc/%s removed\n", procfs_name);
}

module_init(usg_init_module);
module_exit(usg_exit_module);
