/* NAND FLASH 控制器 */
#define NFCONF (*((volatile unsigned long *)0x4E000000))
#define NFCONT (*((volatile unsigned long *)0x4E000004))
#define NFCMMD (*((volatile unsigned char *)0x4E000008))
#define NFADDR (*((volatile unsigned char *)0x4E00000C))
#define NFDATA (*((volatile unsigned char *)0x4E000010))
#define NFSTAT (*((volatile unsigned char *)0x4E000020))

/*
 * Standard NAND flash commands
 */
#define NAND_CMD_READ0		0
#define NAND_CMD_READ1		1
#define NAND_CMD_RNDOUT		5
#define NAND_CMD_PAGEPROG	0x10
#define NAND_CMD_READOOB	0x50
#define NAND_CMD_ERASE1		0x60
#define NAND_CMD_STATUS		0x70
#define NAND_CMD_STATUS_MULTI	0x71
#define NAND_CMD_SEQIN		0x80
#define NAND_CMD_RNDIN		0x85
#define NAND_CMD_READID		0x90
#define NAND_CMD_ERASE2		0xd0
#define NAND_CMD_RESET		0xff

/* Extended commands for large page devices */
#define NAND_CMD_READSTART	0x30
#define NAND_CMD_RNDOUTSTART	0xE0
#define NAND_CMD_CACHEDPROG	0x15

struct boot_nand_t {
	int page_size;
	int block_size;
	int bad_block_offset;
//	unsigned long size;
};

static struct boot_nand_t nand;
static unsigned short nand_read_id(void);
static void nand_read_ll(unsigned int addr, unsigned char *buf, unsigned int len);
static int isBootFromNorFlash(void)
{
	volatile int *p = (volatile int *)0;
	int val;
	val = *p;
	*p = 0x12345678;
	if(*p == 0x12345678)
	{
		/* 写成功, 是 nand 启动 */
		*p = val;
		return 0;
	}
	else
	{
		/* NOR 不能像内存一样写 */
		return 1;
	}
}
int nand_init_ll(void)
{
	unsigned short nand_id;
#define TACLS 7
#define TWRPH0 7
#define TWRPH1 7
	/* 设置时序 */
	NFCONF = (TACLS<<12)|(TWRPH0<<8)|(TWRPH1<<4);
	/* 使能 NAND Flash 控制器, 初始化 ECC, 禁止片选 */
	NFCONT = (1<<4)|(0<<1)|(1<<0);
	
	nand_id = nand_read_id();
	if (0) { /* dirty little hack to detect if nand id is misread */
		unsigned short * nid = (unsigned short *)0x31fffff0;
		*nid = nand_id;
	}	

    if (nand_id == 0xec76 ||		/* Samsung K91208 */
        nand_id == 0xad76 ) {	/*Hynix HY27US08121A*/
		nand.page_size = 512;
		nand.block_size = 16 * 1024;
		nand.bad_block_offset = 5;
	//	nand.size = 0x4000000;
	} else if (nand_id == 0xecf1 ||	/* Samsung K9F1G08U0B */
		   nand_id == 0xecda ||	/* Samsung K9F2G08U0B */
		   nand_id == 0xecd3 )	{ /* Samsung K9K8G08 */
		nand.page_size = 2048;
		nand.block_size = 128 * 1024;
		nand.bad_block_offset = nand.page_size;
	//	nand.size = 0x8000000;
	} else {
		return -1; // hang
	}
}

int copy_code_to_sdram(unsigned char *src, unsigned char *dest, unsigned int len)
{
	int ret = 0;
	int i = 0;
	/* 如果是 NOR 启动 */
	if(isBootFromNorFlash())
	{
		while (i < len)
		{
			dest[i] = src[i];
			i++;
		}
	}
	else
	{
		ret = nand_init_ll();
		nand_read_ll((unsigned int)src, dest, len);
	}

	return 0;
}
void clear_bss(void)
{
	extern int __bss_start, __bss_end;
	int *p = &__bss_start;
	for (; p < &__bss_end; p++)
		*p = 0;
}
static void nand_select(void)
{
	NFCONT &= ~(1<<1);
}
static void nand_deselect(void)
{
	NFCONT |= (1<<1);
}
static void nand_cmd(unsigned char cmd)
{
	volatile int i;
	NFCMMD = cmd;
	for (i = 0; i < 10; i++);
}
static int nand_addr(unsigned int addr)
{
	unsigned int page_num;
#if 0	
	unsigned int col = addr % 2048;
	unsigned int page = addr / 2048;
	volatile int i;
	NFADDR = col & 0xff;
	for (i = 0; i < 10; i++);
	NFADDR = (col >> 8) & 0xff;
	for (i = 0; i < 10; i++);
	NFADDR = page & 0xff;
	for (i = 0; i < 10; i++);
	NFADDR = (page >> 8) & 0xff;
	for (i = 0; i < 10; i++);
	NFADDR = (page >> 16) & 0xff;
	for (i = 0; i < 10; i++);
#endif	
	if (nand.page_size == 512) {
		/* Write Address */
		NFADDR = addr & 0xff;
		NFADDR = (addr >> 9) & 0xff;
		NFADDR = (addr >> 17) & 0xff;
		NFADDR = (addr >> 25) & 0xff;
	} else if (nand.page_size == 2048) {
		page_num = addr >> 11; /* addr / 2048 */
		/* Write Address */
		NFADDR = 0;
		NFADDR = 0;
		NFADDR = page_num & 0xff;
		NFADDR = (page_num >> 8) & 0xff;
		NFADDR = (page_num >> 16) & 0xff;
		nand_cmd(NAND_CMD_READSTART);
	} else {
		return -1;
	}
}
static void nand_wait_ready(void)
{
	while (!(NFSTAT & 1));
}
static unsigned char nand_data(void)
{
	return NFDATA;
}

static unsigned short nand_read_id(void)
{
	unsigned short res = 0;
	nand_cmd(NAND_CMD_READID);
	NFADDR = 0;
	res = nand_data();
	res = (res << 8) | nand_data();
	return res;
}

static void nand_read_ll(unsigned int addr, unsigned char *buf, unsigned int len)
{
	int col = addr % 2048;
	int i = 0;
	/* 1. 选中 */
	nand_select();
	while (i < len)
	{
		/* 2. 发出读命令 00h */
		nand_cmd(0x00);
		/* 3. 发出地址(分 5 步发出) */
		nand_addr(addr);
		/* 4. 发出读命令 30h */
		nand_cmd(0x30);
		/* 5. 判断状态 */
		nand_wait_ready();
		/* 6. 读数据 */
		for (; (col < nand.page_size) && (i < len); col++)
		{
			buf[i] = nand_data();
			i++;
			addr++;
		}
		col = 0;
	}
	/* 7. 取消选中 */
	nand_deselect();
}
