// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Backtrace calling stack", mon_backtrace },
	{"showmappings","Display mappings from begin to end",mon_showmappings}
};

/***** Implementations of basic kernel monitor commands *****/
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3){
		cprintf("Usage: showmappings 0xbegin_addr 0xend_addr\n");
		return 0;
	}
	uint32_t begin =  ROUNDDOWN(strtol(argv[1],NULL,16),PGSIZE);
	uint32_t end = ROUNDUP(strtol(argv[2],NULL,16),PGSIZE);
	cprintf(" %10s %10s %8s %8s %8s \n","PADDR","VADDR","PTE_P","PTE_W","PTE_U");
	for(uint32_t i = begin;i <end;i+=PGSIZE){
		cprintf(" 0x%8x  0x%8x ", i, KADDR(i));
		pte_t* pte = pgdir_walk(kern_pgdir, KADDR(i), 0);
		if(pte){
			cprintf("%8x  %8x  %8x  \n", (*pte & PTE_P)>0, (*pte & PTE_W)>0,(*pte & PTE_U)>0);
		}
		else{
			cprintf("%8x  %8x  %8x  \n", 0, 0, 0);
		}
	}
	return 0;
}
int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

void
do_overflow(void)
{
    cprintf("Overflow success\n");
}

void
start_overflow(void)
{
	// You should use a techique similar to buffer overflow
	// to invoke the do_overflow function and
	// the procedure must return normally.

    // And you must use the "cprintf" function with %n specifier
    // you augmented in the "Exercise 9" to do this job.

    // hint: You can use the read_pretaddr function to retrieve 
    //       the pointer to the function call return address;
	char str[256] = {};
	int nstr = 0;
	// Your code here.
    char* pret_addr = (char *) read_pretaddr();
    uint32_t overflow_addr = (uint32_t) do_overflow;
    int i;
    for (i = 0; i < 4; ++i)
      cprintf("%*s%n\n", pret_addr[i] & 0xFF, "", pret_addr + 4 + i);
    for (i = 0; i < 4; ++i)
      cprintf("%*s%n\n", (overflow_addr >> (8*i)) & 0xFF, "", pret_addr + i);


}

void
overflow_me(void)
{
        start_overflow();
}
int 
mon_backtrace(int argc,char **argv,struct Trapframe *tf){
	overflow_me();
	cprintf("Stack backtrace:\n");
	uint32_t* ebp = (uint32_t*) read_ebp();
	struct Eipdebuginfo info;
	while(ebp!=NULL){
		uint32_t eip = *(ebp+1);
		cprintf("  eip %08x", eip);
		cprintf("  ebp %08x", ebp);
		cprintf("  args");
		cprintf(" %08x", *(ebp+2));
		cprintf(" %08x", *(ebp+3));
		cprintf(" %08x", *(ebp+4));
		cprintf(" %08x", *(ebp+5));
		cprintf(" %08x\n", *(ebp+6));
		debuginfo_eip(eip, &info);
        cprintf("         %s:%u %.*s+%u\n",
        info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip - (uint32_t)info.eip_fn_addr);
      	ebp = (uint32_t *) (*ebp);
	}
	cprintf("Backtrace success\n");
	return 0;
}

int mon_time(int argc,char**argv,struct Trapframe *tf){
	return 0;
}
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");
    cprintf("x=%d y=%d\n", 3);


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}