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

#ifndef USG_DMA_H
#define USG_DMA_H

#define USGDMA_DRIVER_NAME 	"usg"
#define USGDMA_BAR            0x00

#define USGDMA_MAJOR          0x00   /* dynamic major by default */
#define USGDMA_MINOR          0x00   /* value other then null is not */
                                   /* supported by the load script */
#define USGDMA_DEVS           0x01   /* number of devices */
#define DMA_BUFFER_SIZE (4096 * 256)
#define DMA_DTB_SIZE (4096)

#define USG_BAR_HEADER (2)

#define PCI_VENDOR_ID_USGDMA 0x1172

#define USG_BAR_NUM (3)
static const unsigned long bar_min_len[6] =
	{ 32768, 0, 256, 0, 32768, 0 };

#define dmaBufNum (3)

struct dmaBufDescription {
	char name[16];
	unsigned long size;
};

static const struct dmaBufDescription constDmaBufDesc[dmaBufNum] = 
{
	{"table",DMA_DTB_SIZE},
	{"in",DMA_BUFFER_SIZE},
	{"out",DMA_BUFFER_SIZE}
};

struct dmaBuf {
	/** 
	 * kernel virtual address for buffer in Root Complex memory 
	 */
	void *buf_virt;
	/**
	 * bus address for the buffer in Root Complex memory, in
	 * CPU-native endianess
	 */
	dma_addr_t buf_bus;
};
	

/**
 * Descriptor Header, controls the DMA read engine or write engine.
 *
 * The descriptor header is the main data structure for starting DMA transfers.
 *
 * It sits in End Point (FPGA) memory BAR[2] for 32-bit or BAR[3:2] for 64-bit.
 * It references a descriptor table which exists in Root Complex (PC) memory.
 * Writing the rclast field starts the DMA operation, thus all other structures
 * and fields must be setup before doing so.
 *
 * @see ug_pci_express 8.0, tables 7-3, 7-4 and 7-5 at page 7-14.
 * @note This header must be written in four 32-bit (PCI DWORD) writes.
 */
struct ape_chdma_header {
	/**
	 * w0 consists of two 16-bit fields:
	 * lsb u16 number; number of descriptors in ape_chdma_table
	 * msb u16 control; global control flags
	 */
	u32 w0;
	/* bus address to ape_chdma_table in Root Complex memory */
	u32 bdt_addr_h;
	u32 bdt_addr_l;
	/**
	 * w3 consists of two 16-bit fields:
	 * - lsb u16 rclast; last descriptor number available in Root Complex
	 *    - zero (0) means the first descriptor is ready,
	 *    - one (1) means two descriptors are ready, etc.
	 * - msb u16 reserved;
	 *
	 * @note writing to this memory location starts the DMA operation!
	 */
	u32 w3;
} __attribute__ ((packed));

/**
 * Descriptor Entry, describing a (non-scattered) single memory block transfer.
 *
 * There is one descriptor for each memory block involved in the transfer, a
 * block being a contiguous address range on the bus.
 *
 * Multiple descriptors are chained by means of the ape_chdma_table data
 * structure.
 *
 * @see ug_pci_express 8.0, tables 7-6, 7-7 and 7-8 at page 7-14 and page 7-15.
 */
struct ape_chdma_desc {
	/**
	 * w0 consists of two 16-bit fields:
	 * number of DWORDS to transfer
	 * - lsb u16 length;
	 * global control
	 * - msb u16 control;
	 */
	u32 w0;
	/* address of memory in the End Point */
	u32 ep_addr;
	/* bus address of source or destination memory in the Root Complex */
	u32 rc_addr_h;
	u32 rc_addr_l;
} __attribute__ ((packed));

/**
 * Descriptor Table, an array of descriptors describing a chained transfer.
 *
 * An array of descriptors, preceded by workspace for the End Point.
 * It exists in Root Complex memory.
 *
 * The End Point can update its last completed descriptor number in the
 * eplast field if requested by setting the EPLAST_ENA bit either
 * globally in the header's or locally in any descriptor's control field.
 *
 * @note this structure may not exceed 4096 bytes. This results in a
 * maximum of 4096 / (4 * 4) - 1 = 255 descriptors per chained transfer.
 *
 * @see ug_pci_express 8.0, tables 7-9, 7-10 and 7-11 at page 7-17 and page 7-18.
 */
struct usg_chdma_table {
	/* workspace 0x00-0x0b, reserved */
	u32 reserved1[3];
	/* workspace 0x0c-0x0f, last descriptor handled by End Point */
	u32 w3;
	/* the actual array of descriptors
    * 0x10-0x1f, 0x20-0x2f, ... 0xff0-0xfff (255 entries)
    */
	struct ape_chdma_desc desc[255];
} __attribute__ ((packed));

/**
 * Altera PCI Express ('ape') board specific book keeping data
 *
 * Keeps state of the PCIe core and the Chaining DMA controller
 * application.
 */
struct usg_dev {
	/** the kernel pci device data structure provided by probe() */
	struct pci_dev *pci_dev;
	/**
	 * kernel virtual address of the mapped BAR memory and IO regions of
	 * the End Point. Used by map_bars()/unmap_bars().
	 */
	void * __iomem bar[USG_BAR_NUM];
	/* if the device regions could not be allocated, assume and remember it
	 * is in use by another driver; this driver must not disable the device.
	 */
	u8 revision;
	/* interrupt count, incremented by the interrupt handler */
	int irq_count;

	struct dmaBuf buf[dmaBufNum];
	char procout[1024];
	int procpos;
};

void usg_iowrite( u32 a, u32 d, struct usg_dev *dev );
u32 usg_ioread( u32 a, struct usg_dev *dev );

#endif /* USGDMA_DRIVER_H*/
