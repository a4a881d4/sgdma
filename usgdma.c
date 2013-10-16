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


#include "usgdma.h"
#include "usgproc.h"

MODULE_DESCRIPTION("usg");
MODULE_AUTHOR("a4a881d4");
MODULE_LICENSE("GPL");

/**
 * Prototypes for shared functions
 */
static void usg_remove( struct pci_dev *dev );
static int usg_probe( struct pci_dev *dev, const struct pci_device_id *id );
static void unmap_bars(struct usg_dev *usg, struct pci_dev *dev);

/**
 * define devices our driver supports
 */
static struct pci_device_id usg_ids[] = {
	{	PCI_VENDOR_ID_USGDMA,	/*  Interface chip manufacturer     ID */
		0xe001,           	/*  Device ID for the ??? function */
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

static int scan_bars(struct usg_dev *usg, struct pci_dev *dev)
{
	int i;
	for (i = 0; i < USG_BAR_NUM; i++) {
		unsigned long bar_start = pci_resource_start(dev, i);
		if (bar_start) {
			unsigned long bar_end = pci_resource_end(dev, i);
			unsigned long bar_flags = pci_resource_flags(dev, i);
			printk(KERN_DEBUG "BAR%d 0x%08lx-0x%08lx flags 0x%08lx\n",
			  i, bar_start, bar_end, bar_flags);
		}
	}
	return 0;
}

/**
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed, given by the
 * bar_min_len[] array.
 */
static int map_bars(struct usg_dev *usg, struct pci_dev *dev)
{
	int rc;
	int i;
	/* iterate through all the BARs */
	for (i = 0; i < USG_BAR_NUM; i++) {
		unsigned long bar_start = pci_resource_start(dev, i);
		unsigned long bar_end = pci_resource_end(dev, i);
		unsigned long bar_length = bar_end - bar_start + 1;
		usg->bar[i] = NULL;
		/* do not map, and skip, BARs with length 0 */
		if (!bar_min_len[i])
			continue;
		/* do not map BARs with address 0 */
		if (!bar_start || !bar_end) {
            printk(KERN_DEBUG "BAR #%d is not present?!\n", i);
			rc = -1;
			goto fail;
		}
		bar_length = bar_end - bar_start + 1;
		/* BAR length is less than driver requires? */
		if (bar_length < bar_min_len[i]) {
            printk(KERN_DEBUG "BAR #%d length = %lu bytes but driver "
            "requires at least %lu bytes\n", i, bar_length, bar_min_len[i]);
			rc = -1;
			goto fail;
		}
		/* map the device memory or IO region into kernel virtual
		 * address space */
		usg->bar[i] = pci_iomap(dev, i, bar_min_len[i]);
		if (!usg->bar[i]) {
			printk(KERN_DEBUG "Could not map BAR #%d.\n", i);
			rc = -1;
			goto fail;
		}
        printk(KERN_DEBUG "BAR[%d] mapped at 0x%p with length %lu(/%lu).\n", i,
			usg->bar[i], bar_min_len[i], bar_length);
	}
	/* succesfully mapped all required BAR regions */
	rc = 0;
	goto success;
fail:
	/* unmap any BARs that we did map */
	unmap_bars(usg, dev);
success:
	return rc;
}

/**
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct usg_dev *usg, struct pci_dev *dev)
{
	int i;
	for (i = 0; i < USG_BAR_NUM; i++) {
	  /* is this BAR mapped? */
		if (usg->bar[i]) {
			/* unmap BAR */
			pci_iounmap(dev, usg->bar[i]);
			usg->bar[i] = NULL;
		}
	}
}



/**
 * This function is called by the PCI core when 
 * it has a struct pci_dev that it thinks this driver wants to control.
 * 
 * Purpose: initialize the device properly 
 */

static irqreturn_t usg_isr(int irq, void *dev_id)
{
        struct usg_dev *usg = (struct usg_dev *)dev_id;
        if (!usg)
                return IRQ_NONE;
        usg->irq_count++;
        return IRQ_HANDLED;
}

static int usg_probe( struct pci_dev *dev, 
                      const struct pci_device_id *id )
{
	int ret_val = 0;
	int i;
	struct usg_dev *usg = NULL;
        u8 irq_pin, irq_line;

	printk(KERN_DEBUG "probe(dev = 0x%p, pciid = 0x%p)\n", dev, id);

	/* allocate memory for per-board book keeping */
	usg = kzalloc(sizeof(struct usg_dev), GFP_KERNEL);
	if (!usg) {
		printk(KERN_DEBUG "Could not kzalloc() usg driver memory.\n");
		goto err_usg;
	}
	usg->pci_dev = dev;
	dev_set_drvdata(&dev->dev, usg);
	printk(KERN_DEBUG "probe() ape = 0x%p\n", usg);

	for( i=0;i<dmaBufNum;i++ ) {
		usg->buf[i].buf_virt = (void *)pci_alloc_consistent(dev,
			constDmaBufDesc[i].size, 
			&(usg->buf[i].buf_bus)
			);
		usg->buf[i].mmapped=0;
		usg->buf[i].file=NULL;
		if( !usg->buf[i].buf_virt ) {
			printk(KERN_DEBUG "Could not dma_alloc(%d)ate_coherent memory.\n",i);
			goto err_table;
		}
		printk(KERN_DEBUG "virt = %p, bus = 0x%16llx.\n",
                usg->buf[i].buf_virt, (u64)usg->buf[i].buf_bus);

	}
	//wake up the device
	
	ret_val = pci_enable_device(dev);
	if(ret_val!=0){
		printk(KERN_WARNING "function pci_enable_device failed\n");
		goto err_enable;
	}

	pci_set_master(dev);
        /* enable message signaled interrupts */
        ret_val = pci_enable_msi(dev);
        if (ret_val) {
                printk(KERN_DEBUG "Could not enable MSI interrupting.\n");
                usg->msi_enabled = 0;
        } else {
                printk(KERN_DEBUG "Enabled MSI interrupting.\n");
                usg->msi_enabled = 1;
        }
        ret_val = pci_request_regions(dev, DRV_NAME);
        if (ret_val) {
                usg->in_use = 1;
                goto err_regions;
        }
        usg->got_regions = 1;

        ret_val = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq_pin);
        if (ret_val)
                goto err_irq;
        printk(KERN_DEBUG "IRQ pin #%d (0=none, 1=INTA#...4=INTD#).\n", irq_pin);

        ret_val = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq_line);
        if (ret_val) {
                printk(KERN_DEBUG "Could not query PCI_INTERRUPT_LINE, error %d\n", ret_val);
                goto err_irq;
        }
        printk(KERN_DEBUG "IRQ line #%d.\n", irq_line);
        irq_line = dev->irq;
        ret_val = request_irq(irq_line, usg_isr, IRQF_SHARED, DRV_NAME, (void *)usg);
        if (ret_val) {
                printk(KERN_DEBUG "Could not request IRQ #%d, error %d\n", irq_line, ret_val);
                usg->irq_line = -1;
                goto err_irq;
        }
        usg->irq_line = (int)irq_line;
        printk(KERN_DEBUG "Succesfully requested IRQ #%d with dev_id 0x%p\n", irq_line, usg);



	/* show BARs */
	scan_bars(usg, dev);
	/* map BARs */
	ret_val = map_bars(usg, dev);
	if (ret_val)
		goto err_map;
	
	ret_val = 0;
	printk(KERN_DEBUG "probe() successful.\n");
	memset(usg->procout,0,1024);
	sprintf(usg->procout,"noone write\n");
	usg->procpos=strlen(usg->procout)+1;
	proc_r=usg;
	goto end;
		
err_map:
        if (usg->irq_line >= 0)
                free_irq(usg->irq_line, (void *)usg);

err_irq:
        if (usg->msi_enabled)
                pci_disable_msi(dev);
        if (!usg->in_use)
                pci_disable_device(dev);
        if (usg->got_regions)
                pci_release_regions(dev);
err_regions:		
err_enable:
err_table:
	for( i=0;i<dmaBufNum;i++ ) {
		if( usg->buf[i].buf_virt ) {
			pci_free_consistent( dev, 
				constDmaBufDesc[i].size, 
				usg->buf[i].buf_virt, 
				usg->buf[i].buf_bus
			);
		}
	}
	if(usg)
		kfree(usg);
err_usg:
end:
	return ret_val;
}

/**
 * function to be executed when unloading the driver module
 */
static void usg_remove(struct pci_dev *dev)
{
	printk(KERN_DEBUG "usg_driver removing...\n" ); 
	int i;
	
	struct usg_dev *usg = dev_get_drvdata(&dev->dev);
	
	printk(KERN_DEBUG "remove(dev = 0x%p) where dev->dev.driver_data = 0x%p\n", dev, usg);
	if (usg->pci_dev != dev) {
		printk(KERN_DEBUG "dev->dev.driver_data->pci_dev (0x%08lx) != dev (0x%08lx)\n",
		(unsigned long)usg->pci_dev, (unsigned long)dev);
	}
	

	for( i=0;i<dmaBufNum;i++ ) {
		if( usg->buf[i].buf_virt ) {
			pci_free_consistent( dev, 
				constDmaBufDesc[i].size, 
				usg->buf[i].buf_virt, 
				usg->buf[i].buf_bus
			);
		}
	}
	pci_disable_device(dev);
}


/**
 * init module function
 */
static int __init usg_init_module(void)
{
	int ret_val = 0;
	printk( KERN_DEBUG "Module usg_driver init\n" );

	ret_val = pci_register_driver(&usg_driver);
	printk(KERN_DEBUG "register proc file.\n");
	regProcFile();

	return ret_val;
}

/**
 * exit module function
 */
 
static void __exit usg_exit_module(void)
{
	printk( KERN_DEBUG "Module usg_driver exit\n" );

	/*after unregistering all PCI devices bound to this driver
	will be removed*/
	pci_unregister_driver(&usg_driver);
	printk(KERN_DEBUG "deregister proc file.\n");
	deregProcFile();
	
	
}


void usg_iowrite( u32 a, u32 d, struct usg_dev *dev )
{
	void *p = dev->bar[USG_BAR_HEADER];
	p += a;
	printk(KERN_DEBUG "write %p <- %x.\n",p,d);
	iowrite32( d, p );
}

u32 usg_ioread( u32 a, struct usg_dev *dev )
{
	void *p = dev->bar[USG_BAR_HEADER];
	p += a;
	u32 r = ioread32( dev->bar[USG_BAR_HEADER]+a );	
	printk(KERN_DEBUG "read %p -> %x.\n",p,r);
	return r;
}


module_init(usg_init_module);
module_exit(usg_exit_module);
