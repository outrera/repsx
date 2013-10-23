#ifndef __COFF_H__
#define __COFF_H__

/********************** FILE HEADER **********************/

struct external_filehdr {
	unsigned short f_magic;		/* magic number			*/
	unsigned short f_nscns;		/* number of sections		*/
	unsigned long f_timdat;	/* time & date stamp		*/
	unsigned long f_symptr;	/* file pointer to symtab	*/
	unsigned long f_nsyms;		/* number of symtab entries	*/
	unsigned short f_opthdr;	/* sizeof(optional hdr)		*/
	unsigned short f_flags;		/* flags			*/
};

#define	FILHDR	struct external_filehdr
#define	FILHSZ	sizeof(FILHDR)

#endif
