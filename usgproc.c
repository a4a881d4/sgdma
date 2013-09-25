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
#include "usgdma.h"
#include "usgproc.h"

static size_t ctrl_write( struct file *filp, 
						const char __user *buff, 
						unsigned long len, 
						void *data 
						);

static size_t ctrl_read( struct file *filp, 
						const char __user *buff, 
						unsigned long len, 
						void *data 
						);

struct file_operations ctrl_file_op {
	.read = ctrl_read,
	.write = ctrl_write,
};

void regProcFile( struct usg_dev *dev );
{
	dev->usg_Proc_dir = proc_mkdir( USGDMA_PROC_DIR, NULL );
	
	dev->ctrl = proc_create_data( procfs_io_file, 0666,
					dev->usg_Proc_dir,
					ctrl_file_op,
					dev
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

void deregProcFile( struct usg_dev *dev );
{
	remove_proc_entry( dev->ctrl,
		dev->usg_Proc_dir);
	remove_proc_entry( dev->usg_Proc_dir,
		NULL);
	printk(KERN_INFO "/proc/%s/%s /proc/%s remove\n", USGDMA_PROC_DIR,
		    procfs_ctrl_file,
		    USGDMA_PROC_DIR );	
}

static void proc_iowrite( char *buf, struct usg_dev * )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x %x",&addr,&data);
	usg_iowrite( addr, data, dev );	
}

static void proc_ioread( char *buf, struct usg_dev * )
{
	u32 addr;
	u32 data;
	sscanf(buf,"%x",&addr);
	data = usg_ioread( addr, dev );
	sprintf(dev->procout,"%x ",data);	
}

static void proc_info( struct usg_dev * )
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
ctrl_read( char *buffer,
	      char **buffer_location,
	      off_t offset, 
	      int buffer_length, 
	      int *eof, 
	      void *data
	      )
{
	int ret;
	int len;
	
	printk( KERN_INFO "procfile_read (/proc/%s/ctrl len=%d off=%d) called\n", 
		USGDMA_PROC_DIR, 
		buffer_length,
		offset
		);
		
	struct usg_dev *usg = (struct usg_dev *)data;
	
	len = strlen(usg->procout); 
	*eof=1;
	*buffer_location = usg->procout;	
	printk(KERN_INFO "first DW = %08x\n", *(int*)usg->procout);
	
	return len;
}

static ssize_t ctrl_write( struct file *filp, const char __user *buff, unsigned long len, void *data )
{
    int capacity = 255;
    char myBuf[256];
    size_t pos=0;
    if (len > capacity)
    {
        printk(KERN_INFO "too long command\n");
        return -1;
    }
	
	printk(KERN_INFO "procfile_write (/proc/%s/io len=%d) called\n", USGDMA_PROC_DIR, len);
    if (copy_from_user( myBuf, buff, len ))
    {
        return -2;
    }
    
	struct usg_dev *usg = (struct usg_dev *)data;
	
	myBuf[len] = '\0';
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
		proc_info( myBuf+pos, usg );
		goto end;
	}
end:
    return len;
}
