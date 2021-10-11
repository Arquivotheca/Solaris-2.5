/*
 *  Copyrighted as an unpublished work.
 *  (c) Copyright 1990 INTERACTIVE Systems Corporation
 *  All rights reserved.
 * 
 *  RESTRICTED RIGHTS
 * 
 *  These programs are supplied under a license.  They may be used,
 *  disclosed, and/or copied only as permitted under such license
 *  agreement.  Any copy must contain the above copyright notice and
 *  this restricted rights notice.  Use, copying, and/or disclosure
 *  of the programs is strictly prohibited unless otherwise provided
 *  in the license agreement.
 */

#pragma	ident "@(#)test_eisa.c	1.4	93/03/12 SMI"

/*
"test_eisa.c" - a program to test the EISA_CMOS_QUERY ioctl call.

Copyright 1990 Compaq Computer Corporation.
*/

#include	<stdio.h>
#include 	<ctype.h>
#include	<fcntl.h>
#include	<sys/eisarom.h>
#include	<sys/nvm.h>
/*
 * This macro is ugly
 */
#define TO_UCHAR(x) *(unchar *)(&x)

static char data[20 * 1024];

static char *memory_type[] =
{
   "System (base or extended)",
   "Expanded",
   "Virtual",
   "Other"
};
static char *memory_decode[] =
{
   "20 Bits",
   "24 Bits",
   "32 Bits"
};
static char *width[] =
{
   "Byte",
   "Word",
   "Double Word"
};
static char *dma_timing[] =
{
   "ISA Compatible",
   "Type A",
   "Type B",
   "Type C (BURST)",
};

main (argc, argv)
int argc;
char *argv[];
{
   void		hex_dump ();
   eisanvm	nvm;
   char		*dp;
   NVM_SLOTINFO *slot = (NVM_SLOTINFO *)data;
   NVM_FUNCINFO *function;
   int		fd;
   int		index;
   
   
   if ((fd = open ("/dev/eisarom", O_RDONLY)) == -1)
   { 
     perror ("open failed");
     exit (1);
   }
   
   nvm.data = data;
   
   /* This section transforms the arguments entered on the command line
   into the first stage of a stack frame structure that will be
   used to call the "query" function in the guts of the driver. */
   
   if (argv[1] != NULL)
   {
      unsigned key_mask;		/* bit-mask argument to "eisa_nvm". */
      unsigned argument;		/* For creating "key_mask". */
      char *ap = data;
      char **args = &argv[1];
      
      *((int *)ap) = key_mask = atoi (*args++);
      ap += sizeof (int);
      
      for (argument = 1; argument <= 64; argument <<= 1)
      {
         if (key_mask & argument)
         {
            /* An argument has been selected in the mask. */
         
            if (argument & EISA_INTEGER)
            {
               /* The argument is an integer. */
         
               *((int *)ap) = atoi (*args++);
	       ap += sizeof (int);
               if (argument == 4)
	       {
                  *((int *)ap) = atoi (*args++);
	          ap += sizeof (int);
	       }
            }
            else
            {
               /* The argument is a string. */
         
               strcpy (ap, *args);
               ap += strlen (*args++) + 1;
            }
         }
      }
   }
   else
      *(int *)data = 0;	/* No arguments. Kind of a "browse" mode. */
   
   /* This section displays whatever is returned by the EISA CMOS Query. */
   
   if (ioctl (fd, EISA_CMOS_QUERY, &nvm) == -1)
   {
      perror ("ioctl failed");
      exit (1);
   }

   dp = data;
   while (dp < (data + nvm.length))
   {
      printf ("\nSlot %d\n", *((short *)dp));
      slot = (NVM_SLOTINFO *)(dp + sizeof (short));
      printf ("\nBoard id : %c%c%c %x %x\n",
         (slot->boardid[0] >> 2 &0x1f) + 64,
         ((slot->boardid[0] << 3 | slot->boardid[1] >> 5) & 0x1f) + 64,
         (slot->boardid[1] &0x1f) + 64,
         slot->boardid[2],
         slot->boardid[3]);
      printf ("Revision : %x\n", slot->revision);
      printf ("Number of functions : %x\n", slot->functions);
      printf ("Function info : %x\n", TO_UCHAR(slot->fib));
      printf ("Checksum : %x\n", slot->checksum);
      printf ("Duplicate id info : %x\n", TO_UCHAR(slot->dupid));
      
      printf ("\n\tFunctions\n");
      
      function = (NVM_FUNCINFO *)(slot + 1);
      
      while (function < (NVM_FUNCINFO *)slot + slot->functions)
      {
         if (*(unsigned char *)&function->fib && !function->fib.disable)
         {
            printf ("\n\tBoard id : %c%c%c %x %x\n",
               (function->boardid[0] >> 2 &0x1f) + 64,
               ((function->boardid[0] << 3 |
	          function->boardid[1] >> 5) & 0x1f) + 64,
               (function->boardid[1] &0x1f) + 64,
               function->boardid[2],
               function->boardid[3]);
            printf ("\tDuplicate id info : %x\n", TO_UCHAR(function->dupid));
            printf ("\tFunction info : %x\n", TO_UCHAR(function->fib));
            if (function->fib.type)
               printf ("\tType;sub-type : %s\n", function->type);
            
            if (function->fib.data)
            {
               unsigned char *free_form_data = function->un.freeform + 1;
               unsigned char *free_form_end = free_form_data +
	                                       *function->un.freeform;
               printf ("\tFree Form Data :\n\t");
               for (; free_form_data < free_form_end; free_form_data++)
                  printf ("%u ", *free_form_data);
               printf ("\n");
            }
            else
            {
               if (function->fib.memory)
               {
                  NVM_MEMORY *memory = function->un.r.memory;
               
                  printf ("\tMemory info :\n");
                  while (memory < function->un.r.memory + NVM_MAX_MEMORY)
                  {
                     printf ("\t\tMemory Section %d:\n",
		                    memory - function->un.r.memory + 1);
                     printf ("\t\t\tLogical Characteristics:\n");
                     if (memory->config.write)
                        printf ("\t\t\t\tRead/Write\n");
                     else
                        printf ("\t\t\t\tRead Only\n");
                     if (memory->config.cache)
                        printf ("\t\t\t\tCached\n");
                     printf ("\t\t\t\tType is %s\n",
				    memory_type[memory->config.type]);
                     if (memory->config.share)
                        printf ("\t\t\t\tShared\n");
                     printf ("\t\t\tPhysical Characteristics:\n");
                     printf ("\t\t\t\tData Path Width: %s\n",
		                    width[memory->datapath.width]);
                     printf ("\t\t\t\tData Path Decode: %s\n",
		                    memory_decode[memory->datapath.decode]);
                     printf ("\t\t\tBoundaries:\n");
                     printf ("\t\t\t\tStart address: %lx\n",
		                    *(long *)memory->start * 256);
                     printf ("\t\t\t\tSize: %lx\n",
				    (long)(memory->size * 1024));
                     if (memory->config.more)
                        memory++;
                     else
                        break;
                  }
               }
               
               if (function->fib.irq)
               {
                  NVM_IRQ *irq = function->un.r.irq;
               
                  printf ("\tIRQ info :\n");
                  while (irq < function->un.r.irq + NVM_MAX_IRQ)
                  {
                     printf ("\t\tInterrupt Request Line: %d\n", irq->line);
                     if (irq->trigger)
                        printf ("\t\tLevel-triggered\n");
                     else
                        printf ("\t\tEdge-triggered\n");
                     if (irq->share)
                        printf ("\t\tShareable\n");
                     if (irq->more)
                        irq++;
                     else
                        break;
                  }	
               }
               
               if (function->fib.dma)
               {
                  NVM_DMA *dma = function->un.r.dma;
                  
                  printf ("\tDMA info :\n");
                  while (dma < function->un.r.dma + NVM_MAX_DMA)
                  {
                     printf ("\t\tDMA Device %d:\n",
		                        dma - function->un.r.dma + 1);
                     printf ("\t\t\tChannel Number: %d\n", dma->channel);
                     printf ("\t\t\tTransfer Size is %s\n", width[dma->width]);
                     printf ("\t\t\tTransfer Timing is %s\n",
		                        dma_timing[dma->timing]);
                     if (dma->share)
                        printf ("\t\t\tShareable\n");
                     if (dma->more)
                        dma++;
                     else
                        break;
                  }
               }
               
               if (function->fib.port)
               {
                  NVM_PORT *port = function->un.r.port;
               
                  printf ("\tPort info :\n");
                  while (port < function->un.r.port + NVM_MAX_PORT)
                  {
                     printf ("\t\tPort Address: %x\n", port->address);
                     printf ("\t\tSequential Ports: %d\n", port->count);
                     if (port->share)
                        printf ("\t\tShareable\n");
                     if (port->more)
                        port++;
                     else
                        break;
                  }
               }
               
               if (function->fib.init)
               {
                  unsigned char *p;
                  NVM_INIT *init = (NVM_INIT *)function->un.r.init;
                  
                  p = ((unsigned char *)init);
                  printf ("\tInit info :\n");
                  while (p < ((unsigned char *)function->un.r.init) +
		                                    NVM_MAX_INIT)
                  {
                     printf ("\t\taccess type ");
                     switch (init->type)
                     {
                     case NVM_IOPORT_BYTE:
                        printf ("byte\n");
                        break;
                     case NVM_IOPORT_WORD:
                        printf ("word\n");
                        break;
                     case NVM_IOPORT_DWORD:
                        printf ("dword\n");
                        break;
                     default:
                        printf ("reserved - error\n");
                        break;
                     }
                  
                     if (init->mask)
                     {
                        printf ("\t\tuse mask and value\n");
                        switch (init->type)
                        {
                        case NVM_IOPORT_BYTE:
                           p += 5;
                           printf ("\t\tport width byte, value 0x%x mask 0x%x",
                              init->un.byte_vm.value,
                              init->un.byte_vm.mask);
                           break;
                        case NVM_IOPORT_WORD:
                           p += 7;
                           printf ("\t\tport width word, value 0x%x mask 0x%x",
                              init->un.word_vm.value,
                              init->un.word_vm.mask);
                           break;
                        case NVM_IOPORT_DWORD:
                           p += 11;
                           printf ("\t\tport width dword, value 0x%x mask 0x%x",
                              init->un.dword_vm.value,
                              init->un.dword_vm.mask);
                           break;
                        
                        default:
                           printf ("error in port width");
                           break;
                        }
                     }
                     else
                     {
                        printf ("\t\twrite value\n");
                        switch (init->type)
                        {
                        case NVM_IOPORT_BYTE:
                           p += 4;
                           printf ("\t\tport width byte, value 0x%x",
                              init->un.byte_v.value);
                           break;
                        case NVM_IOPORT_WORD:
                           p += 5;
                           printf ("\t\tport width word, value 0x%x",
                              init->un.word_v.value);
                           break;
                        case NVM_IOPORT_DWORD:
                           p += 7;
                           printf ("\t\tport width dword, value 0x%x",
                              init->un.dword_v.value);
                           break;
                        
                        default:
                           printf ("error in port width");
                           break;
                        }
                     }
                     printf (" to port 0x%x\n",init->port);
                     
                     if(init->more == 0) 
                        break;
                     init = (NVM_INIT *)p;
                  } 
               }
            }
         }
         function++;
      }
      dp = (char *)function;
   }
   
   slot = (NVM_SLOTINFO *)data;
   
   dp = data;
   while (dp < (data + nvm.length))
   {
      printf ("\nSlot %d in hex\n", *(short *)dp);
      slot = (NVM_SLOTINFO *)(dp + sizeof (short));
      hex_dump ((char *)slot,12);
   
      printf ("\n\tFunctions\n");
      function = (NVM_FUNCINFO *)(slot + 1);
      while (function < (NVM_FUNCINFO *)slot + slot->functions)
      {
         hex_dump ((char *)function, sizeof(NVM_FUNCINFO));
         function++;
      }
      dp = (char *)function;
   }
}

#ifndef TRUE
#define TRUE 1
#endif

/* hex_dump will make the raw data easier to read */

void
hex_dump (pointer, endseg)
char		*pointer;
unsigned	endseg;
{
   unsigned i, j, endit, last_i_printed;
   int	doit;
   
   for (i = last_i_printed = 0, doit = TRUE; i < endseg; i += 16)
   {
      if (i)
      {
         if ((endseg - i) <= 16)
         {
            doit = TRUE;
         }
         else
         {
            doit = memcmp (pointer + i, pointer + i - 16, 16);
         }
      }
      if (doit)
      {
         if (i > (last_i_printed + 16))
         {
            fputs ("...\n", stdout);
         }
         fprintf (stdout, "%04x %l08x ", last_i_printed = i, pointer + i);
         for (j = i, endit = i + 8; j < endseg  &&  j < endit;)
         {
            fprintf (stdout, "%02x ",
            *(pointer + j++) & 0x00ff);
         }
         while (j < endit)
         {
            fputs ("   ", stdout);
            ++j;
         }
         putc (' ', stdout);
         for (endit = i + 16; j < endseg  &&  j < endit;)
         {
            fprintf (stdout, "%02x ", *(pointer + j++) & 0x00ff);
         }
         while (j++ < endit)
         {
            fputs ("   ", stdout);
         }
         for (j = i, endit = i + 8; j < endseg  &&  j < endit;)
         {
            if (isascii (*(pointer + j)) && isprint (*(pointer + j)))
            {
               putc (*(pointer + j++), stdout);
            }
            else
            {
               putc ('.', stdout);
               ++j;
            }
         }
         putc (' ', stdout);
         for (endit = i + 16; j < endseg  &&  j < endit;)
         {
            if (isascii (*(pointer + j)) && isprint (*(pointer + j)))
            {
               putc (*(pointer + j++), stdout);
            }
            else
            {
               putc ('.', stdout);
               ++j;
            }
         }
         putc ('\n', stdout);
      }
   }
}
