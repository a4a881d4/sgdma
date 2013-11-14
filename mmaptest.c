#include<stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <asm/byteorder.h>
#include <asm/types.h>
#include <endian.h>

typedef unsigned long int u64;
typedef unsigned int u32;
#define cpu_to_le32 htole32
#define le32_to_cpu le32toh
#define MAX_LINE 256
#define DMA_DTB_NUM			511		/* Descriptor's count.*/

struct dmaBufDesc {
	char name[16];  /* Name of this DMA */
	unsigned long size; /* The size of the DMA buffer */
	u64 virt;   /* The virtual address in user space */
	u64 bus;    /* The physical bus address */
	void *mem;  /* after mmap, save the virtual addr of user space here */
};

/* The global variable sgBus is read form "/proc/usg/ctrl", which exchanges
 * kernel DMA buffer information to user space */
struct dmaBufs {
	int num;
	struct dmaBufDesc bufs[16];
} sgBufs;

/* mmap the /prof files to user space so that user space get a virtual address */
static void *mmap_buf( const char *fn, int len )
{
	int fd = open(fn,O_RDWR);
	void *table = mmap( 0, len, PROT_READ|PROT_WRITE,MAP_SHARED,fd,0 );
	if( !table )
		printf("mmap %s fail\n",fn);
	return table;
}

/* Read one line from FILE */
static char *Readline( char *in, FILE *fp ) 
{
	char *cptr;
	
	do {
		cptr = fgets(in, MAX_LINE, fp);
		if( cptr==NULL )
			return NULL;
		while(*cptr == ' ' || *cptr == '\t') {
			cptr++;
		}
    } while ( *cptr=='#' );

    return cptr;    
}


	/*
	 *	the following code is for demo
	 *	please rewrite with python or bash
	 */
	 
static void send2Ctrl(char *cmd)
{
	FILE *fp = fopen("/proc/usg/ctrl","wt");
	fprintf(fp,"%s",cmd);
	fclose(fp);
}

static int findChar( char *str, int len, char c )
{
	int i;
	for( i=0;i<len;i++ )
		if( str[i]==c )
			return i;
	return -1;
}

/* Fill in sgBus global variable according kernel side info
 * To work properly, kernel or user side should fill buf info correctly */
static void buildBuf()
{
	char line[MAX_LINE+1];
	char temp1[16],temp2[16];
	FILE *fp = fopen("/proc/usg/ctrl","rt");
	char *rec = NULL;
	int pos=-1;
	sgBufs.num = 0;
	send2Ctrl("i");
	while(rec=Readline(line,fp)) {
		if( *rec!='v' )
			continue;
		strncpy(sgBufs.bufs[sgBufs.num].name,rec+2,5);
		pos = findChar(sgBufs.bufs[sgBufs.num].name,5,']');
		if( pos==-1 )
			continue;
		sgBufs.bufs[sgBufs.num].name[pos]='\0';
		rec+=(pos+2);
		pos = findChar(rec,strlen(rec),'x');
		if( pos==-1 )
			continue;
		sscanf(rec+pos+1,"%lx",&sgBufs.bufs[sgBufs.num].virt);
		rec+=pos+1;
		pos = findChar(rec,strlen(rec),'x');
		if( pos==-1 )
			continue;
		sscanf(rec+pos+1,"%lx",&sgBufs.bufs[sgBufs.num].bus);
		rec+=pos+1;
		pos = findChar(rec,strlen(rec),'x');
		if( pos==-1 )
			continue;
		sscanf(rec+pos+1,"%lx",&sgBufs.bufs[sgBufs.num].size);
		printf("v[%s]\t=0x%lx | b[%s]\t=0x%lx | size[%s]\t=0x%lx\n",
			sgBufs.bufs[sgBufs.num].name,
			sgBufs.bufs[sgBufs.num].virt,
			sgBufs.bufs[sgBufs.num].name,
			sgBufs.bufs[sgBufs.num].bus,
			sgBufs.bufs[sgBufs.num].name,
			sgBufs.bufs[sgBufs.num].size
		);
		sprintf(line,"/proc/usg/%s",sgBufs.bufs[sgBufs.num].name);

		/* mmap kernel allocated memory into kernel space */
		sgBufs.bufs[sgBufs.num].mem=mmap_buf(line,sgBufs.bufs[sgBufs.num].size);
		sgBufs.num++;
	}
}

	/* New @see ug_pci_express 11.0, tables 15-5 ~ 15-13 start from page 15-14, p260.
	 * 
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
	struct ape_chdma_desc desc[DMA_DTB_NUM];
} __attribute__ ((packed));



/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define pci_dma_h(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define pci_dma_l(addr) (addr & 0xffffffffUL)

/* As desc is mmapped into PCIE Bar, this function set pcie control registers directly 
 * @addr            The pcie root complex's address of PC host 
 * @param ep_addr   The pcie endpoint's address, which is translated by DMA logic in FPGA */
static inline void ape_chdma_desc_set(struct ape_chdma_desc *desc, u64 addr, u32 ep_addr, int len)
{
	desc->w0 = cpu_to_le32(len / 4);  /* unit is DWORD */
	desc->w0 |= 1<<17; /* Enable EPLAST_ENA bit for each descriptor */
	desc->ep_addr = cpu_to_le32(ep_addr);
	desc->rc_addr_h = cpu_to_le32(pci_dma_h(addr));
	desc->rc_addr_l = cpu_to_le32(pci_dma_l(addr));
}

static inline int compare(u32 *p, u32 *q, int len)
{
	int result = -1;
	int fail = 0;
	int i;
	for (i = 0; i < len / 4; i++) {
		if (*p == *q) {
			/* every so many u32 words, show equals */
			if ((i & 255) == 0)
				printf("[%p] = 0x%08x    [%p] = 0x%08x\n", p, *p, q, *q);
		} 
		else {
			fail++;
			/* show the first few miscompares */
			if (fail < 10)
			        printf("[%p] = 0x%08x != [%p] = 0x%08x ?!\n", p, *p, q, *q);
			        /* but stop after a while */
			else if (fail == 10)
			        printf("---more errors follow! not printed---\n");
			else
			        /* stop compare after this many errors */
			break;
		}
		p++;
		q++;
	}
	if (!fail)
		result = 0;
	return result;
}

/* Using a proc file, which inputs formatted string, 
 * as user to kernel communication interface.
 * This is write BAR 2, which maps to descriptor header in fact.
 */
static void iowrite32( u32 d, u32 a )
{
	FILE *fp = fopen("/proc/usg/ctrl","wt");
	fprintf(fp,"w %x %x",a,d);
	fclose(fp);	
}

/* tab : table's bus address; 
 * tab_p: table's virtual address in user space 
 * in : input buffer's bus address
 */
static int dma_read( u64 tab, u64 in, void *tab_p )
{
	int i, n = 0,PAGE_SIZE=4096;
	u32 w;
	struct usg_chdma_table *ptab = (struct usg_chdma_table *)tab_p;

	/* Set last descriptor record to 644222. Why ?? */
	ptab->w3 = cpu_to_le32(0x0000FADE);
	n = 0;

	/* refill the descritor table, ep_addr is 4096,RC host addr is 'in', size is 2048 */
	for (i = 0; i < DMA_DTB_NUM; i++)
		ape_chdma_desc_set(&ptab->desc[i], in, 4096,  PAGE_SIZE/2);

	n = i - 1; /* i.e DMA_DTB_NUM */

	/*Enable MSI when last descriptor is completed */
	ptab->desc[n].w0 |= cpu_to_le32(1UL << 16);


/*
	Table 15每5. Chaining DMA Control Register Definitions (Note 1)
	Addr
	(2) Register Name 3124 2316 150
	0x0 DMA Wr Cntl DW0 Control Field (refer to Table 15每6) Number of descriptors in descriptor table
	0x4 DMA Wr Cntl DW1 Base Address of the Write Descriptor Table (BDT) in the RC Memory每Upper DWORD
	0x8 DMA Wr Cntl DW2 Base Address of the Write Descriptor Table (BDT) in the RC Memory每Lower DWORD
	0xC DMA Wr Cntl DW3 Reserved RCLAST每Idx of last descriptor to process
	0x10 DMA Rd Cntl DW0 Control Field (refer to Table 15每6) Number of descriptors in descriptor table
	0x14 DMA Rd Cntl DW1 Base Address of the Read Descriptor Table (BDT) in the RC Memory每Upper DWORD
	0x18 DMA Rd Cntl DW2 Base Address of the Read Descriptor Table (BDT) in the RC Memory每Lower DWORD
	0x1C DMA Rd Cntl DW3 Reserved RCLAST每Idx of the last descriptor to process
*/

	/* Set Chaining DMA Read Control Registers */
	int read_head=0x10; /*Read Control Registers Offset */
	iowrite32(pci_dma_h(tab), read_head+4); /* RC Addr High */
	iowrite32(pci_dma_l(tab), read_head+8); /* RC Addr Low */
	iowrite32(n, read_head+0xc); /* Last Descriptor Index */

/*
	Table 15每6. Bit Definitions for the Control Field in the DMA Write Control Register and DMA Read Control Register
	Bit Field Description
	16 Reserved 〞
	17 MSI_ENA
	Enables interrupts of all descriptors. When 1, the endpoint DMA module issues an
	interrupt using MSI to the RC when each descriptor is completed. Your software
	application or BFM driver can use this interrupt to monitor the DMA transfer status.
	18 EPLAST_ENA
	Enables the endpoint DMA module to write the number of each descriptor back to
	the EPLAST field in the descriptor table. Table 15每10 describes the descriptor
	table.
	[24:20] MSI Number
	When your RC reads the MSI capabilities of the endpoint, these register bits map to
	the IP Compiler for PCI Express back-end MSI signals app_msi_num [4:0]. If there
	is more than one MSI, the default mapping if all the MSIs are available, is:
	MSI 0 = Read
	MSI 1 = Write
*/

    w = (u32)(n + 1); /* n is decreased by one before, so restore it by add one */
	w |= (1UL << 18); /* 
	iowrite32(w, read_head+0); /* Number of Descriptors + global EPLAST_EN */

	/* Read 16 tims of w3,which records the number 
	of the last descriptor completed by the chaining DMA module. */
	for( i=0;i<16;i++ )
	{
		volatile u32 *p = &ptab->w3;
		printf("EPLAST = %lu\n", le32_to_cpu(*p) & 0xffffUL);
	}
}


int main(int argc, char *argv[])
{
	int i;
	buildBuf();
	/* write 0xFFFF to offset 0x10 of USG_BAR_HEADER , i.e BAR2
 	 * BTW, I did not find any words about why set it like below.
	 */
	iowrite32(0xffff, 0x10);
	iowrite32(0xffff, 0x10);

	/* bufs[0].bus : Descriptor,s table physical address;
	 * bufs[1].bus : Root complex's PCIE bus address as DMA operation's destination; 
	 * bufs[0].mem : Descriptor,s table user virtual address
	 */
	dma_read( sgBufs.bufs[0].bus, sgBufs.bufs[1].bus, sgBufs.bufs[0].mem );
}
