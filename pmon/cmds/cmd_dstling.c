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

//unsigned int led_counter=0;
//#define BOARD_RUN_LED {gpio_set_value(32, led_counter%2);led_counter++;}
#define USER_DATA_POS	0x60000//1 block=16sector 1sector=8page 1page=512byte  512K=8block
#define USER_DATA_SIZE	0x10000//
#define FLASH_SECSIZE	512 //(512-350-64)*1024
#define START_SEND_FILE_CMD "~~"//"LS1X Receive OK."
#define SEND_NEXT_FILE_DATA  printf("%s",START_SEND_FILE_CMD)
unsigned char *received_file_data_buf=NULL;//接收到的文件数据 

/*
void board_run_led()
{
	
	gpio_set_value(32, led_counter%2);//ָʾ����˸
	led_counter++;
}
*/
int read_flash_data_cmd(int ac, char *av[]);
int write_flash_data_cmd(int ac, char *av[]);
int erase_user_space(void);
int flash_erase_sector_cmd(int ac, char *av[]);
int user_load(void);
int recv_data_test_cmd(void);
char CRC16_checkPACK(unsigned char *rec_buff , int rec_num);
char CRC16_check_all_data(unsigned char *data_buf , int data_buf_all_len,int data_buf_frame_len,unsigned char *crc_buf,int crc_len);//(512+2)*2


static const Cmd Cmdss[] = {
	{"Dstling cmd"},
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
	{"recv_data_test_cmd",	"(void)",
			0,
			"recv_data_test_cmd",
			recv_data_test_cmd, 1, 16, 0},
			
	{"erase_sector",	"(startaddr endaddr)",
			0,
			"erase_sector",
			flash_erase_sector_cmd, 1, 16, 0},

	
		
	{0, 0}
};

static void init_cmd __P((void)) __attribute__ ((constructor));

static void init_cmd()
{
	cmdlist_expand(Cmdss, 1);
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


void show_hex(char * p,unsigned int len)
{
	int i=0;
	for(i=0;i<len;i++)
		printf("%02X ",(unsigned char)p[i]);
	printf("\n");
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
		return;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return;
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
		return;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return;
	}
	addr=str_to_hex(pt);
	write_data_len=strlen(av[2]);
	printf("addr:0x%08X write_data_len:%d\n",addr,write_data_len);

	user_data_write(av[2],write_data_len);

	
    return (1);
}

int erase_user_space(void)
{
	printf("Wait a minute...\n");
	spi_flash_erase_area(USER_DATA_POS, USER_DATA_POS+NVRAM_SECSIZE, 0x10000);
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
		return;
	}

	char * pt=av[1]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 1 not a hex addr.for example:0x12345678\n");
		return;
	}
	startaddr=str_to_hex(pt);

	pt=av[2]+2;
	ret=panduan_hex(pt);
	if(ret==0)
	{
		printf("arg 2 not a hex addr.for example:0x12345678\n");
		return;
	}
	endaddr=str_to_hex(pt);
	
	printf("erase sector from startaddr:0x%08X to endaddr:0x%08X\n",startaddr,endaddr);

	spi_flash_erase_sector(startaddr,endaddr,0x1000);
    return (1);
}



long load_elf_dst(int fd, char *buf, int *n, int flags);
int user_load(void)
{
	long ep=-1;
	int n=0;
	int flags=0;
	static char buf[2048];
	ep = load_elf_dst(0,buf,&n, flags);
	printf ("user_load Entry address is %08x\n", ep);
	if (ep == -1) {
		fprintf(stderr, "%s: boot failed\n", "nor flash");
		return EXIT_FAILURE;
	}

	if (ep == -2) {
		fprintf(stderr, "%s: invalid file format\n", "nor flash");
		return EXIT_FAILURE;
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


unsigned int now_flash_read_pos=USER_DATA_POS;
int read_dst(void *buf, int size)
{
	read_flash_data(now_flash_read_pos,size,buf);
	now_flash_read_pos+=size;
	return size;
}
unsigned int lseek_dst(unsigned int pos,int whereflag)
{
	if(whereflag==SEEK_SET)
		now_flash_read_pos=USER_DATA_POS+pos;
	return pos;
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

	//i = read (fd, addr + dl_offset, size);
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
		if(myflags&OFLAG)
		highmemset((unsigned long long)(dl_loffset-lastaddr+(unsigned long)addr),0,size);
		else bzero (addr + dl_offset, size);
	    
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
	lseek_dst(*n,0);	
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

int recv_data_test_cmd(void)
{
	struct termio sav;
	unsigned int cnt;
	unsigned char *recv_buf=NULL;//存储文件数据
	int recv_count=0;
	char return_flag=0;

	unsigned char * crc_buf=NULL;//存储接收到的crc数据
	int crc_buf_count=0;
	
	char *pt=NULL,*pt_data=NULL;
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
	
	memset(rec_file_size_temp,0,64);
	
	printf("recv_data_test_cmd.\n");
	ioctl(STDIN, CBREAK, &sav);
	ioctl(STDIN, FIONREAD, &cnt);
	while(cnt)
	{
		fflush(STDIN);
		ioctl(STDIN, FIONREAD, &cnt);
	}
	while(1)
	{
		ioctl (STDIN, FIONREAD, &cnt);
		if(start_rec_flag&&cnt>0&&start_rec_counter<24)//先接收文件发送的相关信息
		{
			for(;cnt!=0&&start_rec_counter<24;start_rec_counter++,cnt--)
				rec_file_size_temp[start_rec_counter]=getchar();

			if(start_rec_counter==24)
			{
				pt=rec_file_size_temp;
				//printf("rec_file_size_temp:%s\n",rec_file_size_temp);
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
					printf("recv file head info check faild:%d.\n",file_leixing_flag);
					fflush(STDIN);
					ioctl(STDIN, TCSETAF, &sav);
					return;
				}
				
				printf("file_len:%d frame_len:%d pass:%d\n",file_len,frame_len,file_leixing_flag);
				recv_buf=(unsigned char *)malloc((file_len+10)*2*sizeof(unsigned char));
				if(recv_buf==NULL)
				{
					printf("req recv_file mem faild.quit recv file.\n");
					return;
				}
				memset(recv_buf,0,(file_len+10)*2);

				crc_buf=(unsigned char *)malloc((file_len/frame_len+3)*4);
				if(crc_buf==NULL)
				{
					printf("req crc_buf mem faild.quit recv file.\n");
					return;
				}
				memset(crc_buf,0,(file_len/frame_len+3)*4);
				
				start_rec_flag=0;
				//printf("%s",START_SEND_FILE_CMD);
				SEND_NEXT_FILE_DATA;
			}

		}
		
		if(!start_rec_flag&&cnt>0)
		{
			memset(stdin_read_temp,0,4096);
			stdin_read_temp_len=read(STDIN,stdin_read_temp,cnt);
			//printf("\ncnt:%d frame_one_time_counter:%d\n%s",cnt,frame_one_time_counter,stdin_read_temp);
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
			if(frame_one_time_counter>=(frame_len+2)*2)//接受完一段文件数据 字符串中最后四个字符是crc16校验数据
			{
				char*ptt=recv_buf;
				ptt=ptt+recv_count-4;//定位到crc位置
				char*p_crc_buf=crc_buf+crc_buf_count;
				strncpy(p_crc_buf,ptt,4);//将crc数据复制进crc_buf;
				crc_buf_count+=4;
				recv_count=recv_count-4;//去掉结尾的四个校验数据
				frame_one_time_counter=0;
				SEND_NEXT_FILE_DATA;
				/*
				//这里进行crc检验 比较浪费时间
				if(CRC16_checkPACK(ptt,(frame_len+2)*2))
				{
					frame_one_time_counter=0;
					recv_count=recv_count-4;//去掉结尾的四个校验数据
					SEND_NEXT_FILE_DATA;
					//BOARD_RUN_LED;
				}
				else//校验失败
				{
					fflush(STDIN);
					ioctl(STDIN, TCSETAF, &sav);
					free(recv_buf);
					printf("CRC check faild\n");
					return;
				}
				*/
			}
		}
		
		if(return_flag)//文件发送结束
		{
			ioctl (STDIN, FIONREAD, &cnt);
			char*ptt=recv_buf;
			ptt=ptt+recv_count-3-4;//去掉结尾的end 定位到crc位置
			char*p_crc_buf=crc_buf+crc_buf_count;
			strncpy(p_crc_buf,ptt,4);//将crc数据复制进crc_buf;
			crc_buf_count+=4;
			
			recv_count=recv_count-4-3;//去掉结尾的四个校验数据和end数据
			printf("\nreceived all file data.\n");
			/*
			ioctl (STDIN, FIONREAD, &cnt);
			char*ptt=recv_buf;
			ptt=ptt+recv_count-frame_one_time_counter;
			if(CRC16_checkPACK(ptt,frame_one_time_counter-3))//去掉结尾的end@
			{
				//frame_one_time_counter=0;
				//recv_count=recv_count-4;//去掉结尾的四个校验数据
				printf("\nrecv end flag\n");
				//BOARD_RUN_LED;
			}
			else//校验失败
			{
				fflush(STDIN);
				ioctl(STDIN, TCSETAF, &sav);
				free(recv_buf);
				printf("tail CRC check faild.\n");
				return;
			}
			*/
			break;
		}
	}
	printf("\nCRC verification.\n");
	pt=recv_buf;
	if(CRC16_check_all_data(pt,recv_count,frame_len,crc_buf,crc_buf_count))
	{
		printf("\nCRC verification successful.\n");
		received_file_data_buf=(unsigned char *)malloc(file_len*sizeof(unsigned char)+10);
		if(received_file_data_buf==NULL)
		{
			printf("req hex_buf mem faild.quit recv file.\n");
			free(recv_buf);
			return;
		}
		memset(received_file_data_buf,0,file_len);
		pt_data=received_file_data_buf;
		unsigned int index=0;
		unsigned char data_now=0x00;
		pt=recv_buf;
		for(cnt=0,index=0;cnt<recv_count-3-4;cnt+=2,index++)//转换为hex
		{
			memset(str_to_int_temp,0,9);
			strncpy(str_to_int_temp,pt+cnt,2);
			data_now=(unsigned char)str_to_hex(str_to_int_temp);
			pt_data[index]=data_now;
			//printf("rec_file_size_temp:%s 0x%02X\n",str_to_int_temp,pt_data[index]);
		}
		free(recv_buf);
		ioctl(STDIN, TCSETAF, &sav);
		fflush(STDIN);


		//下面进入刷写模式
		if(file_leixing_flag==1234)
		{
			printf("\nuser pro is Programing into Flash chip,file_len:%d\n",file_len);
			user_data_write(received_file_data_buf,file_len);
			printf("\nuser pro Programed Flash success.\n");
		}
		else if(file_leixing_flag==3512)
		{
			printf("\nPMON bin is Programing into Flash chip,file_len:%d\n",file_len);
			pmon_data_write(received_file_data_buf,file_len);
			printf("\nPMON bin Programed Flash success.\n");
		}
		free(received_file_data_buf);
		received_file_data_buf=NULL;
	}
	else
		printf("CRC check faild,quit to program into chip\n");
	return 1;
}

void auto_load_user_pro(void)
{
	char buf[LINESZ];
	char *pa;
	char *rd;
	unsigned int dly;
	unsigned int cnt;
	struct termio sav;
	int ret=1;
	
	char * s=NULL;
	s = getenv("user_pro");
	if(s==NULL)
	{
		printf("need set user_pro no .  or yes  :%s\n",s);
		return;
	}
	printf("user_load sta:%s\n",s);
	
	if(strcmp(s,"yes")==0)
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
			printf("load user program int nor flash address:0x%x.\n",USER_DATA_POS);
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
//=================================================================================================
// CRC��λ�ֽ�ֵ��
//----------------
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


/**************************************************************************************************
    NAME : comp_crc16
   INPUT : unsigned char *pack ���ݻ����׵�ַ, unsigned char num �����ֽ���
  OUTPUT : unsigned int ����������λ��ǰ ��λ�ں�MODBUS �涨����
FUNCTION : 
	   CRC16 ���� MODBUSʹ�ù�ʽ��X16+X15+X2+1 ���˲�ͬ��CITT��ʽ
**************************************************************************************************/
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


char CRC16_checkPACK(unsigned char *rec_buff , int rec_num)//(512+2)*2
{
	if(rec_num==0||rec_num%2!=0)
		return 0;
	char ret=0;
	int i=0,j=0;
	unsigned char data_now=0;
	unsigned char str_to_int_temp[9];
	char *pt=rec_buff;
	/*
	printf("\n");
	for(i=0;i<rec_num;i++)
		printf("%c",rec_buff[i]);
	printf("\n");
	printf("rec_num:%d\n",rec_num);
	*/
	unsigned char * hex_change=(unsigned char*)malloc((rec_num-4)/2);
	if(hex_change==NULL)
	{
		printf("Crc16 req mem faild\n");
		return 0;
	}
	memset(hex_change,0,(rec_num-4)/2);
	
	for(i=0,j=0;i<rec_num-4;i+=2,j++)
	{
		memset(str_to_int_temp,0,9);
		strncpy(str_to_int_temp,pt+i,2);
		data_now=(unsigned char)str_to_hex(str_to_int_temp);
		hex_change[j]=data_now;
	}
	memset(str_to_int_temp,0,9);
	strncpy(str_to_int_temp,pt+i,4);//剩余4个转为hex值
	unsigned int rec_CRC=(unsigned int)str_to_hex(str_to_int_temp);//这个是发送过来的crc值
	//printf("str_to_int_temp:%s rec_CRC:%08x now_CRC:%08x.\n",str_to_int_temp,rec_CRC,comp_crc16(hex_change,(rec_num-4)/2));
	//for(i=0;i<rec_num;i++)
	//	printf("%c",rec_buff[i]);
	//printf("\n");
	if(rec_CRC == comp_crc16(hex_change,(rec_num-4)/2))
		ret=1;
	else
		ret=0;
	
	free(hex_change);
	return ret;
}

char CRC16_check_all_data(unsigned char *data_buf , int data_buf_all_len,int data_buf_frame_len,unsigned char *crc_buf,int crc_len)//(512+2)*2
{
	char * p_data_buf=data_buf;
	char * p_crc_buf=crc_buf;
	int now_data_pos=0;
	int now_crc_pos=0;
	
	//char crc_buf_temp[9];
	//unsigned int rec_crc_data=0xffffffff;
	//unsigned int now_crc_data=0xffffffff;
	char ret=1;

	unsigned char *cpy_data_buf=(unsigned char*)malloc(data_buf_frame_len*2+10);
	if(cpy_data_buf==NULL)
	{
		printf("CRC16_check_all_data req mem faild,quit\n");
		return 0;
	}
	for(now_data_pos=0,now_crc_pos=0;now_data_pos<data_buf_all_len&&now_crc_pos<crc_len;now_data_pos+=data_buf_frame_len*2,now_crc_pos+=4)
	{
		memset(cpy_data_buf,0,data_buf_frame_len*2+10);
		p_data_buf=data_buf+now_data_pos;
		p_crc_buf=crc_buf+now_crc_pos;
		if(data_buf_all_len-now_data_pos<data_buf_frame_len*2)//剩余的数据不够一帧了 结束了
		{
			strncpy(cpy_data_buf,p_data_buf,data_buf_all_len-now_data_pos);
		}
		else
		{
			strncpy(cpy_data_buf,p_data_buf,data_buf_frame_len*2);
		}
		strncat(cpy_data_buf,p_crc_buf,4);
		ret=CRC16_checkPACK(cpy_data_buf,strlen(cpy_data_buf));
		if(ret==0)
			break;
		/*memset(crc_buf_temp,0,9);
		p_data_buf=data_buf+now_data_pos;
		p_crc_buf=crc_buf+now_crc_pos;
		strncpy(crc_buf_temp,p_crc_buf,4);
		rec_crc_data=str_to_hex(crc_buf_temp);

		
		now_crc_data=comp_crc16(p_data_buf,data_buf_frame_len);
		printf("rec_crc_data:0x%08x now_crc_data:0x%08x\n",rec_crc_data,now_crc_data);
		if(rec_crc_data!=now_crc_data)
			ret=0;
		*/
	}
	return ret;
}






