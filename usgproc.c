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

#include <linux/proc_fs.h>

#include "usgdma.h"
#include "usgproc.h"

#define WHITESPACE " \t\v\f\n"

static size_t ctrl_write( struct file *filp, 
	const char __user *buf, 
	size_t count, 
	loff_t *f_pos );

static size_t ctrl_read( struct file *filp, 
	char __user *buf, 
	size_t count, 
	loff_t *f_pos );

struct file_operations ctrl_file_op = {
	.read = ctrl_read,
	.write = ctrl_write,
};

void regProcFile( struct usg_dev *dev )
{
	dev->usg_Proc_dir = proc_mkdir( USGDMA_PROC_DIR, NULL );
	
	dev->ctrl = proc_create_data( procfs_ctrl_file, 0666,
					dev->usg_Proc_dir,
					&ctrl_file_op,
					(void *)dev
					);
	if ( dev->ctrl == NULL ) {
		remove_proc_entry( dev->usg_Proc_dir, NULL );
		printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n",
			USGDMA_PROC_DIR,
		    procfs_ctrl_file
			);
		return -ENOMEM;
	}

	printk(KERN_INFO "/proc/%s/%s created\n", USGDMA_PROC_DIR,
		    procfs_ctrl_file);		
}

void deregProcFile( struct usg_dev *dev )
{
	remove_proc_entry( dev->ctrl,
		dev->usg_Proc_dir);
	remove_proc_entry( dev->usg_Proc_dir,
		NULL);
	printk(KERN_INFO "/proc/%s/%s /proc/%s remove\n", USGDMA_PROC_DIR,
		    procfs_ctrl_file,
		    USGDMA_PROC_DIR );	
}

static void proc_iowrite( char *buf, struct usg_dev * dev )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x %x",&addr,&data);
	usg_iowrite( addr, data, dev );	
}

static void proc_ioread( char *buf, struct usg_dev * dev )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x",&addr);
	data = usg_ioread( addr, dev );
	sprintf(dev->procout,"%x ",data);	
}

static void proc_info( struct usg_dev * dev )
{
	size_t pos = 0;
	sprintf(dev->procout+pos,"#info of USG Driver \n");
	pos = strlen(dev->procout);
		
}

static size_t cmpCommand( char *buf, const char *cmd )
{
	size_t ret=0;
	size_t pos=0;
	pos = strspn( buf+pos, WHITESPACE );
    if ( strncmp( buf+pos, cmd, strlen(cmd) ) == 0 ) {
    	ret = pos+strlen(cmd);
    }
    return ret;
}

static size_t
ctrl_read( struct file *filp, 
	char __user *buf, 
	size_t count, 
	loff_t *f_pos)
{
	int ret;
	int len;
	
		
	struct usg_dev *usg = (struct usg_dev *)filp->private_data;
	
	len = strlen(usg->procout); 
	copy_to_user( buf, usg->procout, len+1 );
	printk(KERN_INFO "len = %d, first DW = %08x\n", len, *(int*)usg->procout);
	
	return (size_t)len+1;
}

static size_t ctrl_write( struct file *filp, 
	const char __user *buf, 
	size_t count, 
	loff_t *f_pos )
{
    int capacity = 255;
    char myBuf[256];
    size_t pos=0;
    if (count > capacity)
    {
        printk(KERN_INFO "too long command\n");
        return -1;
    }
	
	printk(KERN_INFO "procfile_write (/proc/%s/io len=%d) called\n", USGDMA_PROC_DIR, count);
    if (copy_from_user( myBuf, buf, count ))
    {
        return -2;
    }
    
	struct usg_dev *usg = (struct usg_dev *)filp->private_data;
	
	myBuf[count] = '\0';
	pos = cmpCommand(myBuf,"w");
	if( pos ) {
		proc_iowrite( myBuf+pos, usg );
		goto end;
	}
	pos = cmpCommand(myBuf,"r");
	if( pos ) {
		proc_ioread( myBuf+pos, usg );
		goto end;
	}
	pos = cmpCommand(myBuf,"i");
	if( pos ) {
		proc_info( usg );
		goto end;
	}
end:
    return count;
}
