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
#include <linux/mm.h>

#include "usgdma.h"
#include "usgproc.h"

#define WHITESPACE " \t\v\f\n"

struct usg_dev *proc_r = NULL;
struct proc_dir_entry *usg_Proc_dir = NULL;

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

static int tab_buf_mmap(struct file *file, struct vm_area_struct * vma)
{
	printk(KERN_INFO "tab buf mmap\n" );
	long int offset = vma->vm_pgoff << PAGE_SHIFT;
	io_remap_pfn_range( vma, 
		vma->vm_start, 
		(proc_r->buf[0].buf_bus + offset) >> PAGE_SHIFT, 
		vma->vm_end - vma->vm_start, 
		vma->vm_page_prot
		);	
}

static int in_buf_mmap(struct file *file, struct vm_area_struct * vma)
{
	printk(KERN_INFO "in buf mmap\n" );
	long int offset = vma->vm_pgoff << PAGE_SHIFT;
	io_remap_pfn_range( vma, 
		vma->vm_start, 
		(proc_r->buf[1].buf_bus + offset) >> PAGE_SHIFT, 
		vma->vm_end - vma->vm_start, 
		vma->vm_page_prot
		);	
}

static int out_buf_mmap(struct file *file, struct vm_area_struct * vma)
{
	printk(KERN_INFO "out buf mmap\n" );
	long int offset = vma->vm_pgoff << PAGE_SHIFT;
	io_remap_pfn_range( vma, 
		vma->vm_start, 
		(proc_r->buf[2].buf_bus + offset) >> PAGE_SHIFT, 
		vma->vm_end - vma->vm_start, 
		vma->vm_page_prot
		);	
}

struct file_operations buf_file_op[3] = {
	{ .mmap = tab_buf_mmap, },
	{ .mmap = in_buf_mmap, },
	{ .mmap = out_buf_mmap, },
};

void regProcFile()
{
	int i;
	usg_Proc_dir = proc_mkdir( USGDMA_PROC_DIR, NULL );
	
	struct proc_dir_entry *ctrl = proc_create( procfs_ctrl_file, 0666,
					usg_Proc_dir,
					&ctrl_file_op
					);
	
	if ( ctrl == NULL ) {
		remove_proc_entry( USGDMA_PROC_DIR, NULL );
		printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n",
			USGDMA_PROC_DIR,
		    procfs_ctrl_file
			);
		return;
	}
	for( i=0;i<dmaBufNum;i++ ) {
		proc_r->buf[i].file = proc_create_data( constDmaBufDesc[i].name, 0666,
					usg_Proc_dir,
					&buf_file_op[i],
					&proc_r->buf[i]
					);
		if ( proc_r->buf[i].file == NULL ) {
			printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n",
				USGDMA_PROC_DIR,
		    	constDmaBufDesc[i].name
				);
		}
	}		
}

void deregProcFile()
{
	int i;
	for( i=0;i<dmaBufNum;i++ ) {
		remove_proc_entry( constDmaBufDesc[i].name,
			usg_Proc_dir);
	}
	remove_proc_entry( procfs_ctrl_file,
		usg_Proc_dir);
	remove_proc_entry( USGDMA_PROC_DIR,
		NULL);
	printk(KERN_INFO "/proc/%s/%s /proc/%s remove\n", USGDMA_PROC_DIR,
		    procfs_ctrl_file,
		    USGDMA_PROC_DIR );	
}

static void proc_iowrite( char *buf )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x %x",&addr,&data);
	/* addr is a offset, data is a value*/
	usg_iowrite( addr, data, proc_r );	
	printk( KERN_INFO "write driver at %p\n", proc_r );
}

static void proc_ioread( char *buf )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x",&addr);
	data = usg_ioread( addr, proc_r );
	sprintf(proc_r->procout,"%x ",data);
	proc_r->procpos=strlen(proc_r->procout)+1;	
	printk( KERN_INFO "read driver at %p\n", proc_r );
}

static void proc_info()
{
	char buf[256];
	int i;
	sprintf(proc_r->procout,"#info of USG Driver \n");
	for( i=0;i<dmaBufNum;i++ ) {
		sprintf(buf,"v[%s]\t=0x%p | b[%s]\t=0x%llx | size[%s]\t=0x%llx\n",
			constDmaBufDesc[i].name,
			proc_r->buf[i].buf_virt,
			constDmaBufDesc[i].name,
			(u64)proc_r->buf[i].buf_bus,
			constDmaBufDesc[i].name,
			constDmaBufDesc[i].size
		);
		strcat(proc_r->procout,buf);
	}
	struct ape_chdma_desc *desc = (struct ape_chdma_desc *)proc_r->buf[0].buf_virt;
	for( i=0;i<4;i++ ) {
		sprintf(buf,"%03d w0:0x%08x | ep_addr:0x%08x | rc_addr_h:0x%08x | rc_addr_l:0x%08x\n",i,
			desc->w0,desc->ep_addr,desc->rc_addr_h,desc->rc_addr_l
			);
		strcat(proc_r->procout,buf);
		desc++;
	}
	sprintf(buf,"int: %d\n",proc_r->irq_count);
	strcat(proc_r->procout,buf);
	proc_r->procpos = strlen(proc_r->procout)+1;
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
	size_t ret;
	int len;
	
		
	struct usg_dev *usg = (struct usg_dev *)proc_r;
	printk( KERN_INFO "driver at %p\n", usg );
	len = strlen(usg->procout); 
	if( usg->procpos<=0 )
		return 0;
	printk(KERN_INFO "len = %d, first DW = %08x %ld\n", len, *(int*)usg->procout, *f_pos);
	copy_to_user( buf, usg->procout, len+1 );
	
	ret = len+1;
	usg->procpos-=ret;
	return ret;
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
	
	printk(KERN_INFO "procfile_write (/proc/%s/io len=%d) called\n", USGDMA_PROC_DIR, (int)count);
    if (copy_from_user( myBuf, buf, count ))
    {
        return -2;
    }
    
	myBuf[count] = '\0';
	pos = cmpCommand(myBuf,"w");
	if( pos ) {
		proc_iowrite( myBuf+pos );
		goto end;
	}
	pos = cmpCommand(myBuf,"r");
	if( pos ) {
		proc_ioread( myBuf+pos );
		goto end;
	}
	pos = cmpCommand(myBuf,"i");
	if( pos ) {
		proc_info( );
		goto end;
	}

end:
    return count;
}
