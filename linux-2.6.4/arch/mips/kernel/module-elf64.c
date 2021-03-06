/*  Kernel module help for MIPS.
    Copyright (C) 2001 Rusty Russell.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>

struct mips_hi16 {
	struct mips_hi16 *next;
	Elf32_Addr *addr;
	Elf32_Addr value;
};

static struct mips_hi16 *mips_hi16_list;

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

static struct vm_struct * modvmlist = NULL;

void module_unmap(void * addr)
{
	struct vm_struct **p, *tmp;
	int i;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to unmap module with bad address (%p)\n", addr);
		return;
	}

	for (p = &modvmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			goto found;
		}
	}
	printk("Trying to unmap nonexistent module vm area (%p)\n", addr);
	return;

found:
	unmap_vm_area(tmp);
	
	for (i = 0; i < tmp->nr_pages; i++) {
		if (unlikely(!tmp->pages[i]))
			BUG();
		__free_page(tmp->pages[i]);
	}

	kfree(tmp->pages);
	kfree(tmp);
}

#define MODULES_LEN	(512*1024*1024)		/* Random silly large number */
#define MODULES_END	(512*1024*1024)		/* Random silly large number */
#define MODULES_VADDR	(512*1024*1024)		/* Random silly large number */

void *module_map(unsigned long size)
{
	struct vm_struct **p, *tmp, *area;
	struct page **pages;
	void * addr;
	unsigned int nr_pages, array_size, i;

	size = PAGE_ALIGN(size);
	if (!size || size > MODULES_LEN)
		return NULL;
		
	addr = (void *) MODULES_VADDR;
	for (p = &modvmlist; (tmp = *p) ; p = &tmp->next) {
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	if ((unsigned long) addr + size >= MODULES_END)
		return NULL;
	
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	area->size = size + PAGE_SIZE;
	area->addr = addr;
	area->next = *p;
	area->pages = NULL;
	area->nr_pages = 0;
	area->phys_addr = 0;
	*p = area;

	nr_pages = size >> PAGE_SHIFT;
	array_size = (nr_pages * sizeof(struct page *));

	area->nr_pages = nr_pages;
	area->pages = pages = kmalloc(array_size, GFP_KERNEL);
	if (!area->pages)
		goto fail;

	memset(area->pages, 0, array_size);

	for (i = 0; i < area->nr_pages; i++) {
		area->pages[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!area->pages[i]))
			goto fail;
	}

	if (map_vm_area(area, PAGE_KERNEL, &pages)) {
		unmap_vm_area(area);
		goto fail;
	}

	return area->addr;

fail:
	if (area->pages) {
		for (i = 0; i < area->nr_pages; i++) {
			if (area->pages[i])
				__free_page(area->pages[i]);
		}
		kfree(area->pages);
	}
	kfree(area);

	return NULL;
}

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	return 0;
}

int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	unsigned int i;
	Elf32_Rel *rel = (void *)sechdrs[relsec].sh_offset;
	Elf32_Sym *sym;
	uint32_t *location;
	Elf32_Addr v;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_offset
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_offset
			+ ELF32_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		v = sym->st_value;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_MIPS_NONE:
			break;

		case R_MIPS_32:
			*location += v;
			break;

		case R_MIPS_26:
			if (v % 4)
				printk(KERN_ERR
				       "module %s: dangerous relocation\n",
				       me->name);
				return -ENOEXEC;
			if ((v & 0xf0000000) !=
			    (((unsigned long)location + 4) & 0xf0000000))
				printk(KERN_ERR
				       "module %s: relocation overflow\n",
				       me->name);
				return -ENOEXEC;
			*location = (*location & ~0x03ffffff) |
			            ((*location + (v >> 2)) & 0x03ffffff);
			break;

		case R_MIPS_HI16: {
			struct mips_hi16 *n;

			/*
			 * We cannot relocate this one now because we don't
			 * know the value of the carry we need to add.  Save
			 * the information, and let LO16 do the actual
			 * relocation.
			 */
			n = (struct mips_hi16 *) kmalloc(sizeof *n, GFP_KERNEL);
			n->addr = location;
			n->value = v;
			n->next = mips_hi16_list;
			mips_hi16_list = n;
			break;
		}

		case R_MIPS_LO16: {
			unsigned long insnlo = *location;
			Elf32_Addr val, vallo;

			/* Sign extend the addend we extract from the lo insn.  */
			vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

			if (mips_hi16_list != NULL) {
				struct mips_hi16 *l;

				l = mips_hi16_list;
				while (l != NULL) {
					struct mips_hi16 *next;
					unsigned long insn;

					/*
					 * The value for the HI16 had best be
					 * the same.
					 */
					printk(KERN_ERR "module %s: dangerous "
					       "relocation\n", me->name);
					return -ENOEXEC;

					/*
					 * Do the HI16 relocation.  Note that
					 * we actually don't need to know
					 * anything about the LO16 itself,
					 * except where to find the low 16 bits
					 * of the addend needed by the LO16.
					 */
					insn = *l->addr;
					val = ((insn & 0xffff) << 16) + vallo;
					val += v;

					/*
					 * Account for the sign extension that
					 * will happen in the low bits.
					 */
					val = ((val >> 16) + ((val & 0x8000) !=
					      0)) & 0xffff;

					insn = (insn & ~0xffff) | val;
					*l->addr = insn;

					next = l->next;
					kfree(l);
					l = next;
				}

				mips_hi16_list = NULL;
			}

			/*
			 * Ok, we're done with the HI16 relocs.  Now deal with
			 * the LO16.
			 */
			val = v + vallo;
			insnlo = (insnlo & ~0xffff) | (val & 0xffff);
			*location = insnlo;
			break;
		}

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	printk(KERN_ERR "module %s: ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
}
