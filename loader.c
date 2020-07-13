#include <stdio.h>
#include <stdlib.h>
#include "simos.h"

// need to be consistent with paging.c: mType and constant definitions
#define opcodeShift 24
#define operandMask 0x00ffffff
#define diskPage -2

FILE *progFd;

//==========================================
// load program into memory and build the process, called by process.c
// a specific pid is needed for loading, since registers are not for this pid
//==========================================

// may return progNormal or progError (the latter, if the program is incorrect)
int load_instruction (mType *buf, int page, int offset)
{ 
  // load instruction to buffer
	int opcode,operand;
		opcode=buf->mInstr >> opcodeShift;
		//opcode=opcode << opcodeShift;
		operand=buf->mInstr & operandMask;
		printf ("Load instruction (%x, %d) into M(%d, %d) \n", opcode << opcodeShift, operand, page,offset );
		
}

int load_data (mType *buf, int page, int offset)
{ 
  // load data to buffer (same as load instruction, but use the mData field
	float data;	
		data=buf->mData;
		printf ("Load Data: %.2f into M(%d, %d) \n",data, page,offset);
		
}

// load program to swap space, returns the #pages loaded
int load_process_to_swap (int pid, char *fname)
{ 
  // read from program file "fname" and call load_instruction & load_data
  // to load the program into the buffer, write the program into
  // swap space by inserting it to swapQ
  // update the process page table to indicate that the page is not empty
  // and it is on disk (= diskPage)
	  FILE *fprog;
	  int msize, numinstr, numdata;
	  int ret, i,j, opcode, operand;
	  float data;
	  int ProcPpages;
	  mType *buf;

	  // a program in file fname is submitted,
	  // it needs msize memory, has numinstr of instructions and numdata of data
	  // assign pid, allocate PCB and memory

	  init_process_pagetable (pid);
	  fprog = fopen (fname, "r");
	  if (fprog == NULL)
	  { printf ("Submission Error: Incorrect program name: %s!\n", fname);
	    return progError;
	  }
	  
	  ret = fscanf (fprog, "%d %d %d\n", &msize, &numinstr, &numdata);
	  if (ret < 3)   // did not get all three inputs
	  { printf ("Submission failure: missing %d program parameters!\n", 3-ret);
	    return progError;
	  }
	   printf("Program info: %d %d %d\n", msize, numinstr, numdata);
	
	//ProcPpages=ceil((msize)/(pageSize))
	if(((msize)%(pageSize))>0){ProcPpages=((msize)/(pageSize))+1;}
	else if(((msize)%(pageSize))==0){ProcPpages=((msize)/(pageSize));}
	
	
        if(ProcPpages > maxPpages){
		  return progError;
	  }
        else{
         //mType *buf = (mType *) malloc (pageSize*sizeof(mType));
	  // load instructions and data of the process to memory 
	int m=0;        
	for (i=0; i<ProcPpages; i++)
	 {
         
	 buf = (mType *) malloc (pageSize*sizeof(mType));
         bzero(buf,sizeof(buf));	
	while(m<msize){
	 for (j=0; j<pageSize; j++)
	  { 
            if(m<numinstr) { 
		fscanf (fprog, "%d %d\n", &opcode, &operand);
		opcode = opcode << opcodeShift;
                operand = operand & operandMask;
		buf[j].mInstr = opcode | operand; 
		load_instruction (&buf[j], i, j);
		}
	    else if(m>=numinstr && m <msize){ 
		fscanf (fprog, "%f\n", &data);
		buf[j].mData = data;
		 load_data (&buf[j], i, j);
		}
	    m++;				
	   }
         break;
	  }
	  update_process_pagetable (pid, i, diskPage);
	  insert_swapQ(pid, i, buf, actWrite, freeBuf);
	  }
          
	  close (fprog);
         return ProcPpages;
	  }

}

int load_pages_to_memory (int pid, int numpage)
{
  // call insert_swapQ to load the pages of process pid to memory
  // #pages to load = min (loadPpages, numpage = #pages loaded to swap for pid)
  // ask swap.c to place the process to ready queue only after the last load
  // do not forget to update the page table of the process
  // this function has some similarity with page fault handler
      int i;
      printf("Load %d pages for process %d\n",numpage,pid);
      dump_process_pagetable (pid);
      int load = (numpage > loadPpages) ? loadPpages : numpage;
      //int load = MIN (loadPpages, numpage);
      //mType buf;
      for(i=0; i<load; i++){

	load_into_memory(pid,i,load);
	
	}
    return load;
}

int load_process (int pid, char *fname)
{ int ret,ret_loaded;

  ret = load_process_to_swap (pid, fname);   // return #pages loaded
  if (ret != progError) ret_loaded=load_pages_to_memory (pid, ret);
  return (ret_loaded);
}

// load idle process, idle process uses OS memory
// We give the last page of OS memory to the idle process
#define OPifgo 5   // has to be consistent with cpu.c
void load_idle_process ()
{ int page, frame;
  int instr, opcode, operand, data;

  init_process_pagetable (idlePid);
  page = 0;   frame = OSpages - 1;
  update_process_pagetable (idlePid, page, frame);
  update_frame_info (frame, idlePid, page);
  
  // load 1 ifgo instructions (2 words) and 1 data for the idle process
  opcode = OPifgo;   operand = 0;
  instr = (opcode << opcodeShift) | operand;
  direct_put_instruction (frame, 0, instr);   // 0,1,2 are offset
  direct_put_instruction (frame, 1, instr);
  direct_put_data (frame, 2, 1);
}

