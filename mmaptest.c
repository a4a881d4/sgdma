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
    } while ( *cptr=="#" );

    return cptr;    
}

struct dmaBufDesc {
	char name[16];
	unsigned long size;
	u64 virt;
	u64 bus;
	void *mem;
};

struct dmaBufs {
	int num;
	struct dmaBufDesc bufs[16];
} sgBufs;

static void send2Ctrl(char *cmd)
{
	FILE *fp = fopen("/proc/usg/ctrl","wt");
	fprintf(fp,"%s",cmd);
	fclose(fp);
}

static void buildBuf()
{
	char line[MAX_LINE+1];
	char temp1[16],temp2[16];
	FILE *fp = fopen("/proc/usg/ctrl","rt");
	char *rec = NULL;
	sgBufs.num = 0;
	send2Ctrl("i");
	while(rec=Readline(line,fp)) {
		sscanf(rec,"v[%s]\t=0x%lx | b[%s]\t=0x%lx | size[%s]\t=%lx\n",
			sgBuf.bufs[sgBufs.num].name,
			&sgBuf.bufs[sgBufs.num].virt,
			temp1,
			&sgBuf.bufs[sgBufs.num].bus,
			temp2,
			&sgBuf.bufs[sgBufs.num].size
		);
		printf("v[%s]\t=0x%lx | b[%s]\t=0x%lx | size[%s]\t=%lx\n",
			sgBuf.bufs[sgBufs.num].name,
			sgBuf.bufs[sgBufs.num].virt,
			sgBuf.bufs[sgBufs.num].name,
			sgBuf.bufs[sgBufs.num].bus,
			sgBuf.bufs[sgBufs.num].name,
			sgBuf.bufs[sgBufs.num].size
		);
	}
	/*
	int *tab = mmap_buf("/proc/usg/tab",0x1000);
	int *in = mmap_buf("/proc/usg/in",4096 * 256);
	int *out = mmap_buf("/proc/usg/tab",4096 * 256);

	*/
}
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

int *mmap_buf( const char *fn, int len )
{
	int fd = open(fn,O_RDWR);
	int *table = mmap( 0, len, PROT_READ|PROT_WRITE,MAP_SHARED,fd,0 );
	if( !table )
		printf("mmap %s fail\n",fn);
	return table;
}

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define pci_dma_h(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define pci_dma_l(addr) (addr & 0xffffffffUL)

static inline void ape_chdma_desc_set(struct ape_chdma_desc *desc, u64 addr, u32 ep_addr, int len)
{
	desc->w0 = cpu_to_le32(len / 4);
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
static void iowrite32( u32 d, u32 a )
{
	FILE *fp = fopen("/proc/usg/ctrl","wt");
	fprintf(fp,"w %x %x",a,d);
	fclose(fp);	
}
static int dma_read( u64 tab, u64 in, void *tab_p )
{
	int i, n = 0,PAGE_SIZE=4096;
	u32 w;
	struct usg_chdma_table *ptab = (struct usg_chdma_table *)tab_p;
	ptab->w3 = cpu_to_le32(0x0000FADE);
	n = 0;
	/* read 8192 bytes from RC buffer to EP address 4096,because 32to64 ,so alloc 2*1024 */
	//ape_chdma_desc_set(&ape->table_virt->desc[n], buffer_bus, 4096, 2 * PAGE_SIZE);
	ape_chdma_desc_set(&ptab->desc[n], in, 4096, PAGE_SIZE/2);
	for (i = 0; i < 255; i++)
		ape_chdma_desc_set(&ptab->desc[i], in, 4096,  PAGE_SIZE/2);
	/* index of last descriptor */
	n = i - 1;
    w = (u32)(n + 1);
    /*global EPLAST_EN*/
	w |= (1UL << 18);
	iowrite32(w, 0);
	iowrite32(pci_dma_h(tab), 4);
	iowrite32(pci_dma_l(tab), 8);
	iowrite32(n, 0xc);
	printf("EPLAST = %lu\n", le32_to_cpu(ptab->w3) & 0xffffUL);
}
int main(int argc, char *argv[])
{
	int i;
	buildBuf();
	//dma_read( tab_bus, in_bus, (void *)tab );
}