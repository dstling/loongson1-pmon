/* $Id: set.c,v 1.1.1.1 2006/09/14 01:59:08 root Exp $ */

#include <stdio.h>
#include <termio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _KERNEL
#undef _KERNEL
#include <sys/ioctl.h>
#define _KERNEL
#else
#include <sys/ioctl.h>
#endif
#include <fcntl.h>
#include <exec.h>
#include <pmon/loaders/loadfn.h>
#include <pmon.h>
#include <file.h>
#include <mtdfile.h>
#include <unistd.h>
#include "../loaders/elf.h"
#ifdef __mips__
#include <machine/cpu.h>
#endif
#include <sys/param.h>

int elfreadsyms(int fd, Elf32_Ehdr *eh, Elf32_Shdr *shtab, int flags);
static unsigned long tablebase;
static int	bootseg;
static int myflags;
static unsigned int lastaddr=0;;
extern unsigned long long dl_loffset;

#define USER_DATA_POS	0x60000//1 block=16sector 1sector=8page 1page=512byte  512K=8block
#define USER_DATA_SIZE	0x10000//
#define FLASH_SECSIZE	512 //(512-350-64)*1024
#define START_SEND_FILE_CMD "~~"//"LS1X Receive OK."
#define SEND_NEXT_FILE_DATA  printf("%s",START_SEND_FILE_CMD)
unsigned char *received_file_data_buf=NULL;//接收到的文件数据 

struct in_flash_file_str
{
	unsigned int nand_pro_len;//nand中存储的用户程序文件长度
	unsigned int file_data_crc;//存储的crc值
};

#define USING_NOR 	1
#define USING_NAND 	2
char load_nor_or_nand=0;//1 nor 2 nand

char nand_mtd_name[128]="";//用于读写nand中裸机程序路径
int read_nand_fp=0;
int nand_pro_len=0;//nand中存储的用户程序文件长度
struct in_flash_file_str  flash_file_head={0,0xffffffff};


unsigned int now_flash_read_pos=0;//当前读取的文件位置


/*
unsigned int led_counter=0;
#define BOARD_RUN_LED {gpio_set_value(32, led_counter%2);led_counter++;}
void board_run_led()
{
	
	gpio_set_value(32, led_counter%2);//ָʾ����˸
	led_counter++;
}
*/


unsigned int comp_crc16(unsigned char *pack, unsigned char num); 

int read_flash_data_cmd(int ac, char *av[]);
int write_flash_data_cmd(int ac, char *av[]);
int erase_user_space(int ac, char *av[]);
int flash_erase_sector_cmd(int ac, char *av[]);
int user_load(int ac, char *av[]);

void write_into_nand_cmd(int ac, char *av[]);
void read_from_nand_cmd(int ac, char *av[]);

int write_into_nand_data(char * buf,int buf_len,unsigned int addr);
int read_from_nand_data(int addr,char*buf,int buf_len);

int recv_hex_data(int ac, char *av[]);

unsigned int str_to_hex(char * p)
{
	unsigned int tempx=0;
	int i=0;
	for(i=0;p[i]!='\0';i++)
	{
		switch(p[i])
		{
		case '0':tempx<<=4;tempx+=0;break;
		case '1':tempx<<=4;tempx+=1;break;
		case '2':tempx<<=4;tempx+=2;break;
		case '3':tempx<<=4;tempx+=3;break;
		case '4':tempx<<=4;tempx+=4;break;
		case '5':tempx<<=4;tempx+=5;break;
		case '6':tempx<<=4;tempx+=6;break;
		case '7':tempx<<=4;tempx+=7;break;
		case '8':tempx<<=4;tempx+=8;break;
		case '9':tempx<<=4;tempx+=9;break;
		case 'A':case 'a':tempx<<=4;tempx+=10;break;
		case 'B':case 'b':tempx<<=4;tempx+=11;break;
		case 'C':case 'c':tempx<<=4;tempx+=12;break;
		case 'D':case 'd':tempx<<=4;tempx+=13;break;
		case 'E':case 'e':tempx<<=4;tempx+=14;break;
		case 'F':case 'f':tempx<<=4;tempx+=15;break;
		default:break;
		}
	}
	return tempx;
}

bool panduan_hex(char * p)
{
	int i=0;
	for(i=0;p[i]!='\0';i++)
	{
		switch(p[i])
		{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case 'A':case 'a':
		case 'B':case 'b':
		case 'C':case 'c':
		case 'D':case 'd':
		case 'E':case 'e':
		case 'F':case 'f':break;
		default:return 0;
		}
	}
	return 1;
}

int find_desc_from_src(int from,char * src,unsigned int src_len,char * desc,unsigned int desc_len)
{
	int i=from;//由于从src结尾附近找 从这里开始
	int j=0;
	if(i<0)//指令过长
		return 0;
	for(;i<src_len;i++)//没到结尾
	{
		for(j=0;j<desc_len;j++)
		{
			if(desc[j]==src[i+j])//如果相同 比对下一个
				continue;
			else
				break;
		}
		if(desc[j]=='\0')//成功比对到结尾了
			return i;//返回当前找到的字符串开始位置
	}
	return -1;
}

void show_hex(char * p,unsigned int len)
{
	int i=0;
	printf("\n");
	for(i=0;i<len;i++)
		printf("%02X ",(unsigned char)p[i]);
	printf("\n");
}

void dstling(int ac, char *av[])
{
	printf("STDIN:%d\n",STDIN);
	printf("kbd_available:%d\n",kbd_available);
	printf("usb_kbd_available:%d\n",usb_kbd_available);
}

static const Cmd Cmdss[] = {
	{"Dstling cmd"},
	{"dstling",	"void",
			0,
			"dstling",
			dstling, 1, 16, 0},
	{"write_flash_data",	"addr datas",
			0,
			"write_flash_data_cmd",
			write_flash_data_cmd, 1, 16, 0},
	{"read_flash_data",		"addr len",
			0,
			"read_flash_data",
			read_flash_data_cmd, 1, 16, 0},
	{"erase_user_space", 	"(void)",
			0,
			"erase_user_space",
			erase_user_space, 1, 16, 0},
	{"user_load",	"(void)",
			0,
			"user_load",
			user_load, 1, 16, 0},
			
	{"erase_sector",	"(startaddr endaddr)",
			0,
			"erase_sector",
			flash_erase_sector_cmd, 1, 16, 0},
	{"write_into_nand",	"(/dev/mtdx buf buf_len)",
			0,
			"write_into_nand",
			write_into_nand_cmd, 1, 16, 0},
	{"read_from_nand", "(/dev/mtdx offset datas_len)",
			0,
			"read_from_nand",
			read_from_nand_cmd, 1, 16, 0},
	{"recv_hex_data", "(void)",
			0,
			"recv_hex_data",
			recv_hex_data, 1, 16, 0},
	
	{0, 0}
};

static void init_cmd __P((void)) __attribute__ ((constructor));

static void init_cmd()
{
	cmdlist_expand(Cmdss, 1);
}

void init_nand_or_nor_env(void)
{
	char * s=NULL;
	s = getenv("user_pro");
	if(s==NULL)
	{
		printf("need set user_pro no .  or yes  :%s\n",s);
		return;
	}
	//printf("user_load sta:%s\n",s);
	
	memset(nand_mtd_name,0,128);
	if(find_desc_from_src(0,s,strlen(s),"/dev/mtd",8)==0)
	{
		strncpy(nand_mtd_name,s,strlen(s));
		load_nor_or_nand=USING_NAND;
		now_flash_read_pos=0;
		printf("We use nand flash for user pro:%s\n",nand_mtd_name);
	}
	else if(strcmp(s,"nor_flash")==0)
	{
		load_nor_or_nand=USING_NOR;
		now_flash_read_pos=USER_DATA_POS;
		printf("We use nor flash for user pro:%s\n",s);
	}
	else 
		printf("We use what for user pro:%s??\n",s);
}


void user_data_write(char *buffer,int buffer_size)
{
	if(buffer_size>USER_DATA_SIZE)
	{
		printf("WARNNING:buffer size above the flash reserve size.buffer size:%d reserve flash size:%d\n",buffer_size,USER_DATA_SIZE);
		return ;
	}
	spi_flash_erase_area(USER_DATA_POS, USER_DATA_POS+buffer_size, 0x10000);
	spi_flash_write_area(USER_DATA_POS, buffer, buffer_size);
}

void pmon_data_write(char *buffer,int buffer_size)
{
	spi_flash_erase_area(0, 0x60000, 0x10000);
	spi_flash_write_area(0, buffer, buffer_size);
}

void read_flash_data(unsigned int addr,unsigned int read_len,char *buffer)
{
	if(read_len<1||buffer==0)
		return;
	spi_flash_read_area(addr, buffer, read_len);
}

int read_flash_data_cmd(int ac, char *av[])
{
	char ret=0;
	unsigned int addr=0;
	unsigned int read_data_len=0;
	unsigned int readed_len=0;
	char readbuf[512];
	printf("read_flash_data_cmd().\n");
	if(ac<3)
	{
		printf("useage: read_flash_data addr 32\n");
		return 0;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return 0;
	}
	addr=str_to_hex(pt);
	read_data_len=atoi(av[2]);
	printf("addr:0x%08X readlen:%d\n",addr,read_data_len);


	while(readed_len<read_data_len)
	{
		if(read_data_len-readed_len>512)
		{
			read_flash_data(addr,512,readbuf);
			show_hex(readbuf,512);
			addr+=512;
			readed_len+=512;
		}
		else
		{
			read_flash_data(addr,read_data_len-readed_len,readbuf);
			show_hex(readbuf,read_data_len-readed_len);
			break;
		}
	}
	
    return (1);
}


int write_flash_data_cmd(int ac, char *av[])
{
	char ret=0;
	unsigned int addr=0;
	unsigned int write_data_len=0;
	printf("write_flash_data_cmd().\n");
	if(ac<3)
	{
		printf("useage: write_flash_data_cmd addr 32\n");
		return 0;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return 0;
	}
	addr=str_to_hex(pt);
	write_data_len=strlen(av[2]);
	printf("addr:0x%08X write_data_len:%d\n",addr,write_data_len);

	user_data_write(av[2],write_data_len);

	
    return (1);
}

int erase_user_space(int ac, char *av[])
{
	init_nand_or_nor_env();
	if(load_nor_or_nand==USING_NOR)
	{
		printf("Wait a minute...\n");
		spi_flash_erase_area(USER_DATA_POS, USER_DATA_POS+NVRAM_SECSIZE, 0x10000);
    }
	else if(load_nor_or_nand==USING_NAND)
	{
		char cmdx[256];
		memset(cmdx,0,256);
		strcat(cmdx,"mtd_erase ");
		strcat(cmdx,nand_mtd_name);
		do_cmd(cmdx);
	}
	return (1);
}

//#include <target/ls1x_spi.h>
//extern struct spi_device spi_flash;

int flash_erase_sector_cmd(int ac, char *av[])
{
	char ret=0;
	unsigned int startaddr=0,endaddr=0;
	printf("write_flash_data_cmd().\n");
	if(ac<3)
	{
		printf("useage: erase_sector startaddr endaddr\n");
		return 0;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return 0;
	}
	startaddr=str_to_hex(pt);

	pt=av[2]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 2 not a hex addr.for example:0x12345678\n");
		return 0;
	}
	endaddr=str_to_hex(pt);
	
	printf("erase sector from startaddr:0x%08X to endaddr:0x%08X\n",startaddr,endaddr);

	spi_flash_erase_sector(startaddr,endaddr,0x1000);
    return (1);
}

int read_dst(void *buf, int size)
{
	if(load_nor_or_nand==USING_NOR)
	{
		read_flash_data(now_flash_read_pos,size,buf);
		now_flash_read_pos+=size;
		return size;
	}
	else if(load_nor_or_nand==USING_NAND&&read_nand_fp)
	{
		int readed_len=read(read_nand_fp,buf,size);
		now_flash_read_pos+=readed_len;
		return  readed_len;
	}
	else
		return 0;
	
}
unsigned int lseek_dst(unsigned int pos,int whereflag)
{
	if(whereflag==SEEK_SET)
	{
		if(load_nor_or_nand==USING_NOR)
		{
			now_flash_read_pos=USER_DATA_POS+pos;
			return pos;
		}
		else if(load_nor_or_nand==USING_NAND&&read_nand_fp)
		{
			now_flash_read_pos=lseek(read_nand_fp, pos, SEEK_SET);
			return now_flash_read_pos;
		}
		else
			return 0;
	}
	return 0;
}

static void *gettable(int size, char *name, int flags)
{
	unsigned long base;

	if( !(flags & KFLAG)) {
		/* temporarily use top of memory to hold a table */
		base = (tablebase - size) & ~7;
		if (base < dl_maxaddr) {
			fprintf (stderr, "\nnot enough memory for %s table", base);
			return 0;
		}
		tablebase = base;
	}
	else {
		/* Put table after loaded code to support kernel DDB */
		tablebase = roundup(tablebase, sizeof(long));
		base = tablebase;
		tablebase += size;
	}
	return (void *) base;
}

int bootread_dst(int fd, void *addr, int size)
{
	int i;

	if (bootseg++ > 0)
		fprintf (stderr, "\b + ");
	fprintf (stderr, "0x%x/%d ", addr + dl_offset, size);
	if (!dl_checksetloadaddr (addr + dl_offset, size, 1))
		return (-1);

	i = read_dst(addr + dl_offset, size);
	if (i < size) 
	{
		if (i >= 0)
			fprintf (stderr, "\nread failed (corrupt object file?)");
		else
			perror ("\nsegment read");
		return (-1);
	}
	return size;
}


static Elf32_Shdr *elfgetshdr_dst(int fd, Elf32_Ehdr *ep)
{
	Elf32_Shdr *shtab;
	unsigned size = ep->e_shnum * sizeof(Elf32_Shdr);

	shtab = (Elf32_Shdr *) malloc (size);
	if (!shtab) {
		fprintf (stderr,"\nnot enough memory to read section headers");
		return (0);
	}

	if (lseek_dst(ep->e_shoff, SEEK_SET) != ep->e_shoff ||
	    read_dst(shtab, size) != size) 
	{
		perror ("\nsection headers");
		free (shtab);
		return (0);
		
	}

	return (shtab);
}

static void *readtable_dst(int fd, int offs, void *base, int size, char *name, int flags)
{
	if (lseek_dst (offs, SEEK_SET) != offs ||
	    read_dst(base, size) != size) 
	{
		fprintf (stderr, "\ncannot read %s table", name);
		return 0;
	}
	return (void *) base;
}

int elfreadsyms_dst(int fd, Elf32_Ehdr *eh, Elf32_Shdr *shtab, int flags)   //static 
{
	Elf32_Shdr *sh, *strh, *shstrh, *ksh;
	Elf32_Sym *symtab;
	Elf32_Ehdr *keh;
	char *shstrtab, *strtab, *symend;
	int nsym, offs, size, i;
	int *symptr;

	//Fix up twirl 
	if (bootseg++ > 0) {
		fprintf (stderr, "\b + ");
	}

	/*
	 *  If we are loading symbols to support kernel DDB symbol handling
	 *  make room for an ELF header at _end and after that a section
	 *  header. DDB then finds the symbols using the data put here.
	 */
	if(flags & KFLAG) {
		tablebase = roundup(tablebase, sizeof(long));
		symptr = (int *)tablebase;
		tablebase += sizeof(int *) * 2;
		keh = (Elf32_Ehdr *)tablebase;
		tablebase += sizeof(Elf32_Ehdr); 
		tablebase = roundup(tablebase, sizeof(long));
		ksh = (Elf32_Shdr *)tablebase;
		tablebase += roundup((sizeof(Elf32_Shdr) * eh->e_shnum), sizeof(long)); 
		memcpy(ksh, shtab, roundup((sizeof(Elf32_Shdr) * eh->e_shnum), sizeof(long)));
		sh = ksh;
	}
	else {
		sh = shtab;
	}
	shstrh = &sh[eh->e_shstrndx];

	for (i = 0; i < eh->e_shnum; sh++, i++) {
		if (sh->sh_type == SHT_SYMTAB) {
			break;
		}
	}
	if (i >= eh->e_shnum) {
		return (0);
	}

	if(flags & KFLAG) {
		strh = &ksh[sh->sh_link];
		nsym = sh->sh_size / sh->sh_entsize;
		offs = sh->sh_offset;
		size = sh->sh_size;
		fprintf (stderr, "%d syms ", nsym);
	} else {
		strh = &shtab[sh->sh_link];
		nsym = (sh->sh_size / sh->sh_entsize) - sh->sh_info;
		offs = sh->sh_offset + (sh->sh_info * sh->sh_entsize);
		size = nsym * sh->sh_entsize;
		fprintf (stderr, "%d syms ", nsym);
	}



	/*
	 *  Allocate tables in correct order so the kernel grooks it.
	 *  Then we read them in the order they are in the ELF file.
	 */
	shstrtab = gettable(shstrh->sh_size, "shstrtab", flags);
	strtab = gettable(strh->sh_size, "strtab", flags);
	symtab = gettable(size, "symtab", flags);
	symend = (char *)symtab + size;


	do {
		if(shstrh->sh_offset < offs && shstrh->sh_offset < strh->sh_offset) 
		{

			memset(shstrtab, 0, shstrh->sh_size);
			strcpy(shstrtab + shstrh->sh_name, ".shstrtab");
			strcpy(shstrtab + strh->sh_name, ".strtab");
			strcpy(shstrtab + sh->sh_name, ".symtab");
			shstrh->sh_offset = 0x7fffffff;
		}

		if (offs < strh->sh_offset && offs < shstrh->sh_offset) 
		{
			if (!(readtable_dst(fd, offs, (void *)symtab, size, "sym", flags))) 
			{
				return (0);
			}
			offs = 0x7fffffff;
		}

		if (strh->sh_offset < offs && strh->sh_offset < shstrh->sh_offset) 
		{
			if (!(readtable_dst (fd, strh->sh_offset, (void *)strtab,
					 strh->sh_size, "string", flags))) 
			{
				return (0);
			}
			strh->sh_offset = 0x7fffffff;
		}
		if (offs == 0x7fffffff && strh->sh_offset == 0x7fffffff &&
		    shstrh->sh_offset == 0x7fffffff) {
			break;
		}
	} while(1);


	if(flags & KFLAG) 
	{
		/*
		 *  Update the kernel headers with the current info.
		 */
		shstrh->sh_offset = (Elf32_Off)shstrtab - (Elf32_Off)keh;
		strh->sh_offset = (Elf32_Off)strtab - (Elf32_Off)keh;
		sh->sh_offset = (Elf32_Off)symtab - (Elf32_Off)keh;
		memcpy(keh, eh, sizeof(Elf32_Ehdr));
		keh->e_phoff = 0;
		keh->e_shoff = sizeof(Elf32_Ehdr);
		keh->e_phentsize = 0;
		keh->e_phnum = 0;

		printf("\nKernel debugger symbols ELF hdr @ %p", keh);

		symptr[0] = (int)keh;
		symptr[1] = roundup((int)symend, sizeof(int));

	}
	else 
	{

		/*
		 *  Add all global sybols to PMONs internal symbol table.
		 */
		for (i = 0; i < nsym; i++, symtab++) {
			int type;

			dotik (4000, 0);
			if (symtab->st_shndx == SHN_UNDEF ||
			    symtab->st_shndx == SHN_COMMON) {
				continue;
			}

			type = ELF_ST_TYPE (symtab->st_info);
			if (type == STT_SECTION || type == STT_FILE) {
				continue;
			}

			/* only use globals and functions */
			if (ELF_ST_BIND(symtab->st_info) == STB_GLOBAL ||
			    type == STT_FUNC){
				if (symtab->st_name >= strh->sh_size) {
					fprintf (stderr, "\ncorrupt string pointer");
					return (0);
				}
			}
			if (!newsym (strtab + symtab->st_name, symtab->st_value)) {
				fprintf (stderr, "\nonly room for %d symbols", i);
				return (0);
			}
		}
	}
	return (1);
}

static int bootclear_dst(int fd, void *addr, int size)
{

	if (bootseg++ > 0)
		fprintf (stderr, "\b + ");
	fprintf (stderr, "0x%x/%d(z) ", addr + dl_offset, size);

	if (!dl_checkloadaddr (addr + dl_offset, size, 1))
		return (-1);

	if (size > 0)
	{
		if(myflags&OFLAG)
			highmemset((unsigned long long)(dl_loffset-lastaddr+(unsigned long)addr),0,size);
		else 
			bzero (addr + dl_offset, size);
	}
	    
	return size;
}


long load_elf_dst(int fd, char *buf, int *n, int flags)
{
	Elf32_Ehdr *ep;
	Elf32_Phdr *phtab = 0;
	Elf32_Shdr *shtab = 0;
	unsigned int nbytes;
	int i;
	Elf32_Off highest_load = 0;

	bootseg = 0;
	myflags=flags;

#ifdef __mips__
	tablebase = PHYS_TO_CACHED(memorysize);
#else
	tablebase = memorysize;
#endif
	lseek_dst(*n,SEEK_SET);	
	ep = (Elf32_Ehdr *)buf;
	if (sizeof(*ep) > *n) //52>0 yes
	{
		lseek_dst(*n,0);	
		*n += read_dst(buf+*n, sizeof(*ep)-*n);
		if (*n < sizeof(*ep)) 
			return -1;
	}

	/* check header validity */ 
	if (ep->e_ident[EI_MAG0] != ELFMAG0 ||ep->e_ident[EI_MAG1] != ELFMAG1 ||ep->e_ident[EI_MAG2] != ELFMAG2 ||ep->e_ident[EI_MAG3] != ELFMAG3) 
		return (-1);

	fprintf (stderr, "(elf)\n");
	{
		char *nogood = (char *)0;

		if (ep->e_ident[EI_CLASS] != ELFCLASS32)//no
			nogood = "not 32-bit";
		else if(
#if BYTE_ORDER == BIG_ENDIAN
			 ep->e_ident[EI_DATA] != ELFDATA2MSB	//ep->e_ident[EI_DATA]==1
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
			 ep->e_ident[EI_DATA] != ELFDATA2LSB	//no
#endif
			  )
			nogood = "incorrect endianess";			//no
		else if (ep->e_ident[EI_VERSION] != EV_CURRENT)//ep->e_ident[EI_VERSION]==1
			nogood = "version not current";
		else if (
#ifdef powerpc
			 ep->e_machine != EM_PPC
#else 
/* default is MIPS */
#define GREENHILLS_HACK
#ifdef GREENHILLS_HACK
			 ep->e_machine != 10 && //ep->e_machine ==8
#endif
			 ep->e_machine != EM_MIPS
#endif
			  )
			nogood = "incorrect machine type";

		if (nogood) //no nogood==NULL
		{
			fprintf (stderr, "Invalid ELF: %s\n", nogood);
			return -2;
		}
	}
	/* Is there a program header? */
	if (ep->e_phoff == 0 || ep->e_phnum == 0 ||ep->e_phentsize != sizeof(Elf32_Phdr)) //no
	{
		fprintf (stderr, "missing program header (not executable)\n");
		return (-2);
	}
	/* Load program header */
#if _ORIG_CODE_
	nbytes = ep->e_phnum * sizeof(Elf32_Phdr);
#else
	/* XXX: We need to figure out why it works by adding 32!!!! */
	nbytes = ep->e_phnum * sizeof(Elf32_Phdr)+32;
#endif
	phtab = (Elf32_Phdr *) malloc (nbytes);
	if (!phtab) 
	{
		fprintf (stderr,"\nnot enough memory to read program headers");
		return (-2);
	}
	if (lseek_dst(ep->e_phoff, SEEK_SET) != ep->e_phoff || 
	    read_dst((void *)phtab, nbytes) != nbytes) 
	{
		perror ("program header");
		free (phtab);
		return (-2);
	}
	/*
	 * From now on we've got no guarantee about the file order, 
	 * even where the section header is.  Hopefully most linkers
	 * will put the section header after the program header, when
	 * they know that the executable is not demand paged.  We assume
	 * that the symbol and string tables always follow the program 
	 * segments.
	 */
	/* read section table (if before first program segment) */
	if (!(flags & NFLAG) && ep->e_shoff < phtab[0].p_offset)
	{
		shtab = elfgetshdr_dst(fd, ep);
	}

	if (!(flags & YFLAG)) 
	{
		/* We cope with a badly sorted program header, as produced by 
		 * older versions of the GNU linker, by loading the segments
		 * in file offset order, not in program header order. */
		while (1) 
		{
			Elf32_Off lowest_offset = ~0;
			Elf32_Phdr *ph = 0;

			/* find nearest loadable segment */
			for (i = 0; i < ep->e_phnum; i++)
			if (phtab[i].p_type == PT_LOAD && phtab[i].p_offset < lowest_offset) 
			{
				ph = &phtab[i];
				lowest_offset = ph->p_offset;
			}
			if (!ph)
				break;		/* none found, finished */
			/* load the segment */
			if (ph->p_filesz) 
			{
				if (lseek_dst(ph->p_offset, SEEK_SET) != ph->p_offset) 
				{
					fprintf (stderr, "seek failed (corrupt object file?)\n");
					if (shtab)
						free (shtab);
					free (phtab);
					return (-2);
				}
				if (bootread_dst(fd, (void *)ph->p_vaddr, ph->p_filesz) != ph->p_filesz) 
				{
					if (shtab) free (shtab);
					free (phtab);
					return (-2);
				}
			}
			if((ph->p_vaddr + ph->p_memsz) > highest_load) 
			{
				highest_load = ph->p_vaddr + ph->p_memsz;
			}
			if (ph->p_filesz < ph->p_memsz)
				bootclear_dst(fd, (void *)ph->p_vaddr + ph->p_filesz, ph->p_memsz - ph->p_filesz);
			ph->p_type = PT_NULL; /* remove from consideration */
		}
	}
	if (flags & KFLAG) 
	{
		highest_load = roundup(highest_load, sizeof(long));
		tablebase = highest_load;
	}
	if (!(flags & NFLAG)) 
	{
		/* read section table (if after last program segment) */
		if (!shtab)
			shtab = elfgetshdr_dst(fd, ep);
		if (shtab) 
		{
			elfreadsyms_dst (fd, ep, shtab, flags);
			free (shtab);
		}
	}
	free (phtab);
	return (ep->e_entry + dl_offset);
}

int user_load(int ac, char *av[])
{
	long ep=-1;
	int n=0;
	int flags=0;
	static char buf[2048];
	
	init_nand_or_nor_env();
	
	if(load_nor_or_nand==USING_NAND)//如果是从nand引导 需要打开mtd文件
	{
		read_nand_fp = open(nand_mtd_name, O_RDONLY);
		if (!read_nand_fp) 
		{
			printf("open file:%s error!\n",av[1]);
			return 1;
		}
		now_flash_read_pos=0;
		lseek(read_nand_fp, 0, SEEK_SET);
	}
	else if(load_nor_or_nand==USING_NOR)
		now_flash_read_pos=USER_DATA_POS;
	
	ep = load_elf_dst(0,buf,&n, flags);
	printf ("user_load Entry address is %08x\n", ep);
	if (ep == -1) 
	{
		fprintf(stderr, "%s: boot failed\n", (load_nor_or_nand==USING_NOR)?"nor flash":nand_mtd_name);
		return EXIT_FAILURE;
	}
	else if (ep == -2) 
	{
		fprintf(stderr, "%s: invalid file format\n",(load_nor_or_nand==USING_NOR)?"nor flash":nand_mtd_name);
		return EXIT_FAILURE;
	}
	else if(load_nor_or_nand==USING_NAND&&!read_nand_fp)//已经读完了 可以关闭了
	{
		close(read_nand_fp);
		read_nand_fp=0;
	}
	
	printf ("user_load Entry address is %08x\n", ep);

	if (md_cachestat())
		flush_cache(DCACHE | ICACHE, NULL);
	md_setpc(NULL, ep);
	if (!(flags & SFLAG)) 
	{
		dl_setloadsyms();
	}
	return 0;
}


void auto_load_user_pro(void)
{
	unsigned int dly;
	unsigned int cnt;
	struct termio sav;
/*	
	char * s=NULL;
	s = getenv("user_pro");
	if(s==NULL)
	{
		printf("need set user_pro no .  or yes  :%s\n",s);
		return;
	}
	printf("user_load sta:%s\n",s);
	
	memset(nand_mtd_name,0,128);
	if(strcmp(s,"/dev/mtd")>0)
	{
		strncpy(nand_mtd_name,s,strlen(s));
		load_nor_or_nand=USING_NAND;
	}
	else if(strcmp(s,"nor_flash")==0)
		load_nor_or_nand=USING_NOR;
*/

	{
		char *d=NULL;
		if(getenv("bootdelay"))
			d = getenv ("bootdelay");
		else
			d = "8";

		if (!d || !atob (&dly, d, 10) || dly < 0 || dly > 99) {
			dly = 1;
		}

		printf("Press any other key to abort load user program.\n");
		ioctl(STDIN, CBREAK, &sav);
		ioctl(STDIN, FIONREAD, &cnt);

		while (dly != 0 && cnt == 0) 
		{
			delay(200000);
			printf ("\b\b%02d", --dly);
			ioctl (STDIN, FIONREAD, &cnt);
		} 
	
		if (cnt > 0 && strchr("\n\r", getchar())) 
		{
			cnt = 0;
		} 
	
		ioctl(STDIN, TCSETAF, &sav);
		putchar('\n');
		if(cnt==0)
		{
			//printf("load user program int nor flash address:0x%x.\n",USER_DATA_POS);
			if(do_cmd("user_load")!=0)
				return;
			do_cmd("g");
		}
	}

}



//===================================crc16

unsigned char CRC_H[] = { 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 
0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 
0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 
0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 
0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 
0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 
0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 
0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 
0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 
0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 
0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 
0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 
0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 
0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 
0x80, 0x41, 0x00, 0xC1, 0x81, 0x40 
} ; 
unsigned char CRC_L[] = { 
0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 
0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD, 
0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 
0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 
0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4, 
0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3, 
0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 
0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4, 
0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 
0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 
0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED, 
0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26, 
0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 
0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 
0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 
0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 
0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 
0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5, 
0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 
0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 
0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C, 
0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 
0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B, 
0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C, 
0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 
0x43, 0x83, 0x41, 0x81, 0x80, 0x40 
} ;


unsigned int comp_crc16(unsigned char *pack, unsigned char num) 
{ 
	unsigned char CRCcode_H = 0XFF;		// ��CRC�ֽڳ�ʼ��
	unsigned char CRCcode_L = 0XFF;		// ��CRC �ֽڳ�ʼ��
	unsigned char index,i=0;			// ��������

	while (num--)
	{ 
		index = CRCcode_L ^ (pack[i++]);	

		CRCcode_L = CRCcode_H ^ CRC_H[index]; 

		CRCcode_H = CRC_L[index]; 
	} 

	return (CRCcode_L << 8 | CRCcode_H);	// MODBUS �涨��λ��ǰ
}

//nand
void write_into_nand_cmd(int ac, char *av[])
{
	if(ac<4)
	{
		printf("useage: write_into_nand /dev/mtdx datas datas_len\n");
		return;
	}

	char * data_buf;
	unsigned int buf_len=atoi(av[3]);
	char * mtd_name=av[1];
	int bs=0x20000;
	int seek=0;
	int writed_len=0;
	int wfp = open(mtd_name, O_RDWR|O_CREAT|O_TRUNC);
	printf("_file[fp1].fs->devname  ret=%d\n",_file[wfp].fs->devname,strncmp(_file[wfp].fs->devname, "mtd", 3));

	if (!strncmp(_file[wfp].fs->devname, "mtd", 3)) 
	{
		mtdpriv *priv;
		mtdfile *p;
		priv = (mtdpriv *)_file[wfp].data;
		p = priv->file;
		if (p->mtd->type == MTD_NANDFLASH) 
		{
			bs = p->mtd->erasesize;
			printf("bs=%d\n",bs);
		}
	}
	if (!wfp) 
	{
		printf("open file error!\n");
		return ;
	}
	
	lseek(wfp, seek*bs, SEEK_SET);
	writed_len=write(wfp, av[2], buf_len);
	printf("writed_len:%d\n",writed_len);
		
	close(wfp);
}

void read_from_nand_cmd(int ac, char *av[])
{
	if(ac<4)
	{
		printf("useage: read_from_nand /dev/mtdx offset datas_len\n");
		return;
	}
	int read_len=atoi(av[3]);
	char * read_buf=(char*)malloc(read_len+10);
	if(!read_buf)
	{
		printf("req read_buf faild\n");
		return;
	}

	char * pt=av[2]+2;
	char ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 2 not a hex addr.for example:0x12345678\n");
		return 0;
	}
	int seek=str_to_hex(pt);
	
	int bs=0x20000;
	int rfp = open(av[1], O_RDONLY);
	if (!rfp) 
	{
		printf("open file:%s error!\n",av[1]);
		free(read_buf);
		return ;
	}
	lseek(rfp, seek, SEEK_SET);//*bs
	int readed_len=read(rfp,read_buf,read_len);
	show_hex(read_buf,readed_len);
	close(rfp);
}

int read_from_nand_data(int addr,char*buf,int buf_len)
{
	if(!read_nand_fp)
		return 0;
	lseek(read_nand_fp, addr, SEEK_SET);
	return read(read_nand_fp,buf,buf_len);
}

int write_into_nand_data(char * buf,int buf_len,unsigned int addr)
{
	int wfp = open(nand_mtd_name, O_RDWR|O_CREAT|O_TRUNC);
	if (!wfp) 
	{
		printf("open file:%s error!\n",nand_mtd_name);
		return ;
	}
	lseek(wfp, addr, SEEK_SET);
	int writed_len=write(wfp, buf, buf_len);
	printf("write_into_nand_data writed_len:%d\n",writed_len);
	close(wfp);
}


#include <sys/sys/termios.h>
int recv_hex_data(int ac, char *av[])
{
	unsigned int cnt;
	unsigned char *recv_buf=NULL;//存储文件数据
	int recv_count=0;
	char return_flag=0;
	
	unsigned char *pt=NULL,*pt_data=NULL;
	char str_to_int_temp[9];
	
	char start_rec_flag=1;
	char start_rec_counter=0;
	char rec_file_size_temp[64];
	int file_len=0;
	int frame_len=0;
	int frame_one_time_counter=0;
	int file_leixing_flag=0;

	char stdin_read_temp[4096];
	int stdin_read_temp_len=0;
	
	struct termio t_sav_arg,t_now_arg;
	
	init_nand_or_nor_env();//初始化环境
	memset(rec_file_size_temp,0,64);
	
	ioctl(STDIN, TCGETA, &t_sav_arg);
	memcpy(&t_now_arg,&t_sav_arg,sizeof(struct termio));
	t_now_arg.c_oflag =0;
	t_now_arg.c_lflag =0;
	t_now_arg.c_iflag =0;
	t_now_arg.c_cflag =0;
	ioctl(STDIN, TCSETAF, &t_now_arg);
	ioctl(STDIN, TCSETAF, &t_now_arg);
	ioctl(STDIN, TCSETAF, &t_now_arg);
	ioctl(STDIN, FIONREAD, &cnt);
	while(cnt)//清空缓存先
	{
		getchar();
		ioctl(STDIN, FIONREAD, &cnt);
	}
	while(1)
	{
		ioctl (STDIN, FIONREAD, &cnt);
		if(start_rec_flag&&cnt>0&&start_rec_counter<24)//先接收文件发送的相关信息
		{
			for(;cnt!=0&&start_rec_counter<24;start_rec_counter++,cnt--)
				rec_file_size_temp[start_rec_counter]=(char)getchar();

			if(start_rec_counter==24)
			{
				pt=rec_file_size_temp;
				memset(str_to_int_temp,0,9);
				strncpy(str_to_int_temp,pt,8);
				file_len=str_to_hex(str_to_int_temp);
				
				memset(str_to_int_temp,0,9);
				strncpy(str_to_int_temp,pt+8,8);
				frame_len=str_to_hex(str_to_int_temp);

				memset(str_to_int_temp,0,9);
				strncpy(str_to_int_temp,pt+16,8);
				file_leixing_flag=str_to_hex(str_to_int_temp);

				if(file_leixing_flag!=3512&&file_leixing_flag!=1234)
				{
					ioctl(STDIN, TCSETAF, &t_sav_arg);
					printf("recv file head info check faild:%d.\n",file_leixing_flag);
					return 0;
				}
				
				//printf("file_len:%d frame_len:%d pass:%d\n",file_len,frame_len,file_leixing_flag);
				recv_buf=(unsigned char *)malloc((file_len+14)*sizeof(unsigned char));
				if(recv_buf==NULL)
				{
					ioctl(STDIN, TCSETAF, &t_sav_arg);
					printf("req recv_file mem faild.quit recv file.\n");
					return 0;
				}
				memset(recv_buf,0,file_len+14);
				
				start_rec_flag=0;
				SEND_NEXT_FILE_DATA;
			}

		}
		
		if(!start_rec_flag&&cnt>0)//继续接收剩下的文件数据
		{
			memset(stdin_read_temp,0,4096);
			stdin_read_temp_len=read(STDIN,stdin_read_temp,cnt);
			int xxi=0;
			for(xxi=0;xxi<stdin_read_temp_len;xxi++,frame_one_time_counter++,recv_count++)
			{
				recv_buf[recv_count]=stdin_read_temp[xxi];
				if(recv_buf[recv_count]=='@'&&recv_count>3&&recv_buf[recv_count-3]=='e'&& recv_buf[recv_count-2]=='n'&& recv_buf[recv_count-1]=='d')
				{
					return_flag=1;
					break;
				}
			}
			if(return_flag==0&&frame_one_time_counter>=frame_len+2)//接受完一段文件数据 数据串中最后2字节是crc16校验数据
			{
				char*ptt=recv_buf;
				ptt=ptt+recv_count-2;//定位到crc位置

				unsigned short crc_rec=0xffff;
				memcpy((char*)&crc_rec,ptt,2);//将crc数据复制进crc_rec;

				if(crc_rec!=comp_crc16(ptt-frame_len,frame_len))//校验失败
				{
					ioctl(STDIN, TCSETAF, &t_sav_arg);
					free(recv_buf);
					printf("CRC verification faild crc_rec:0x%08x,0x%08x\n.",crc_rec,comp_crc16(ptt-frame_len,frame_len));
					return 0;
				}
				else//下一帧数据
				{
					recv_count=recv_count-2;//去掉结尾的2个字节的校验数据
					frame_one_time_counter=0;
					SEND_NEXT_FILE_DATA;
				}
			}
		}
		
		if(return_flag)//文件发送结束
		{
			recv_count=recv_count-2-3;//去掉结尾的2个校验数据和end数据 recv_count最后一次没有+1
			unsigned int last_frame_len=file_len%frame_len;//最后一帧数据的长度
			if(!last_frame_len)//不是正好为空的
			{
				unsigned short crc_rec=0xffff;
				memcpy((char*)&crc_rec,recv_buf+recv_count,2);//将crc数据复制进crc_buf;
				
				if(crc_rec!=comp_crc16(recv_buf+recv_count-last_frame_len,last_frame_len))
				{
					ioctl(STDIN, TCSETAF, &t_sav_arg);
					free(recv_buf);
					printf("Last frame CRC verification faild crc_rec:0x%08x,0x%08x\n.",crc_rec,comp_crc16(recv_buf+recv_count-last_frame_len,last_frame_len));
					return 0;
				}
			}
			break;
		}
	}

	ioctl(STDIN, TCSETAF, &t_sav_arg);//还原term默认状态
	//show_hex(recv_buf,recv_count);
	printf("\nreceived all file data.\n");

	//下面进入刷写模式
	if(file_leixing_flag==1234)
	{
		printf("\nuser pro is Programing into Flash chip,file_len:%d\n",file_len);
		if(load_nor_or_nand==USING_NOR)
			user_data_write(recv_buf,file_len);
		else if(load_nor_or_nand==USING_NAND)
		{
			write_into_nand_data(recv_buf,file_len,0);
			//检查文件写入是否没有问题
			/*
			char * test_read=(char*)malloc(file_len);
			memset(test_read,0,file_len);
			int rfp = open(nand_mtd_name, O_RDONLY);
			if (!rfp) 
			{
				printf("open file:%s error!\n",av[1]);
				return ;
			}
			lseek(rfp, 0, SEEK_SET);//*bs
			int readed_len=read(rfp,test_read,file_len);
			close(rfp);
			
			int ixxx=0;
			printf("jinru ceshi yanzheng\n");
			for(ixxx=0;ixxx<file_len;ixxx++)
			{
				if(test_read[ixxx]!=recv_buf[ixxx])
					printf("error %d:%02x %02x;\n",ixxx,(unsigned char)test_read[ixxx],(unsigned char)recv_buf[ixxx]);
			}
			free(test_read);
			*/
		}
		printf("\nuser pro Programed Flash success.\n");
	}
	else if(file_leixing_flag==3512)
	{
		printf("\nPMON bin is Programing into Flash chip,file_len:%d\n",file_len);
		pmon_data_write(recv_buf,file_len);
		printf("\nPMON bin Programed Flash success.\n");
	}
	
	free(recv_buf);
	return 1;
}



