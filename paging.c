#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "simos.h"

// Memory definitions, including the memory itself and a page structure
// that maintains the informtion about each memory page
// config.sys input: pageSize, numFrames, OSpages
// ------------------------------------------------
// process page table definitions
// config.sys input: loadPpages, maxPpages

mType *Memory;   // The physical memory, size = pageSize*numFrames

typedef unsigned ageType;
typedef struct
{ int pid, page;   // the frame is allocated to process pid for page page
  ageType age;
  char free, dirty, pinned;   // in real systems, these are bits
  int next, prev;
} FrameStruct;

FrameStruct *memFrame;   // memFrame[numFrames]
int freeFhead, freeFtail;   // the head and tail of free frame list

// define special values for page/frame number
#define nullIndex -1   // free frame list null pointer
#define nullPage -1   // page does not exist yet
#define diskPage -2   // page is on disk swap space
#define pendingPage -3  // page is pending till it is actually swapped
   // have to ensure: #memory-frames < address-space/2, (pageSize >= 2)
   //    becuase we use negative values with the frame number
   // nullPage & diskPage are used in process page table 

// define values for fields in FrameStruct
#define zeroAge 0x00000000
#define highestAge 0x00000080
#define dirtyFrame 1
#define cleanFrame 0
#define freeFrame 1
#define usedFrame 0
#define pinnedFrame 1
#define nopinFrame 0

// define shifts and masks for instruction and memory address 
#define opcodeShift 24
#define operandMask 0x00ffffff

// shift address by pagenumShift bits to get the page number
unsigned pageoffsetMask;
int pagenumShift; // 2^pagenumShift = pageSize

//============================
// Our memory implementation is a mix of memory manager and physical memory.
// get_instr, put_instr, get_data, put_data are the physical memory operations
//   for instr, instr is fetched into registers: IRopcode and IRoperand
//   for data, data is fetched into registers: MBR (need to retain AC value)
// page table management is software implementation
//============================


//==========================================
// run time memory access operations, called by cpu.c
//==========================================

// define rwflag to indicate whehter the addr computation is for read or write
#define flagRead 1
#define flagWrite 2

int Data_Exception;

// address calcuation are performed for the program in execution
// so, we can get address related infor from CPU registers
int calculate_memory_address (unsigned offset, int rwflag)
{ 
  // rwflag is used to differentiate the caller
  // different access violation decisions are made for reader/writer
  // if there is a page fault, need to set the page fault interrupt
  // also need to set the age and dirty fields accordingly
  // returns memory address or mPFault or mError

	int pg = offset/pageSize;
	int off = offset%pageSize;
	if(pg > maxPpages){
		return mError;
	}

	if(PCB[CPU.Pid]->PTptr[pg] == diskPage){
		set_interrupt(pFaultException); return mPFault;
	}
	else if(PCB[CPU.Pid]->PTptr[pg] == nullPage && rwflag == flagRead){
		return mError;
	}
	else if(PCB[CPU.Pid]->PTptr[pg] == nullPage && rwflag == flagWrite){
		set_interrupt(pFaultException);	return mPFault;
	}
	else{
	int addr = (off & pageoffsetMask) | (PCB[CPU.Pid]->PTptr[pg] << pagenumShift);
	memFrame[PCB[CPU.Pid]->PTptr[pg]].age=highestAge;	
	return addr;
 	}
}

int get_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy the memory content to MBR
  // return mNormal, mPFault or mError
	  int addr;
	  addr = calculate_memory_address(offset, flagRead);
	  if (addr == mError) return (mError);
	  else if (addr == mPFault){
		Data_Exception=55;
		printf("****** Page Fault: pid/page=(%d,%d)\n",CPU.Pid, (offset/pageSize));
		return (mPFault);
		}
	  else{ 
		CPU.MBR = Memory[addr].mData;
		return (mNormal);
	      }
}

int put_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy MBR to memory 
  // return mNormal, mPFault or mError
	  int addr;
	  int pg=offset/pageSize;
	  addr = calculate_memory_address(offset, flagWrite);
	  if (addr == mError) return (mError);
	  else if (addr == mPFault){
		Data_Exception=55;
		printf("****** Page Fault: pid/page=(%d,%d)\n",CPU.Pid, (offset/pageSize)); 
		CPU.exeStatus = ePFault; 
		return (mPFault);
		}
	  else
	  { 
	    Memory[addr].mData = CPU.MBR;
	    memFrame[PCB[CPU.Pid]->PTptr[pg]].dirty = dirtyFrame;
		return (mNormal);
	  }
	
}

int get_instruction (int offset)
{ 
  // call calculate_memory_address to get memory address
  // convert memory content to opcode and operand
  // return mNormal, mPFault or mError
	 int maddr, instr; 
	 int opcode, operand;
	 maddr=calculate_memory_address (offset, flagRead);
	 if (maddr== mError) return (mError);
	 else if (maddr== mPFault){printf("****** Page Fault: pid/page=(%d,%d)\n",CPU.Pid, (offset/pageSize));return (mPFault);}
	  else
	  { 
	    instr = Memory[maddr].mInstr;
	    CPU.IRopcode  = instr >> opcodeShift;
	    CPU.IRoperand = instr & operandMask;	   
	    return (mNormal);
	  }

}

// these two direct_put functions are only called for loading idle process
// no specific protection check is done
void direct_put_instruction (int findex, int offset, int instr)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mInstr = instr;
}

void direct_put_data (int findex, int offset, mdType data)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mData = data;
}

//==========================================
// Memory and memory frame management
//==========================================

void dump_one_frame (int findex)
{ 
  // dump the content of one memory frame
	int i;
	printf("start-end:%d,%d: ",findex*pageSize,((findex+1)*pageSize));
	for (i=0; i<pageSize; i++){
		int addr = (i & pageoffsetMask) | (findex << pagenumShift);
		printf("%x  ",Memory[addr]);
	}
	printf("\n\n");
}

void dump_memory ()
{ int i;

  printf ("************ Dump the entire memory\n");
  for (i=0; i<numFrames; i++) dump_one_frame (i);
}

// above: dump memory content, below: only dump frame infor

void dump_free_list ()
{ 
  // dump the list of free memory frames  //8 only printed in simos.exe
	int i,counter=0;
	printf("******************** Memory Free Frame Dump\n");
	for (i=OSpages; i<numFrames; i++){
		if(memFrame[i].free == freeFrame){
			printf("%d, ",i);
		 counter++;
		if(counter%8==0){printf("\n");};
		}
	}
printf("\n");
}

void print_one_frameinfo (int indx)
{ printf ("pid/page/age=%d,%d,%x, ",
          memFrame[indx].pid, memFrame[indx].page, memFrame[indx].age);
  printf ("dir/free/pin=%d/%d/%d, ",
          memFrame[indx].dirty, memFrame[indx].free, memFrame[indx].pinned);
  printf ("next/prev=%d,%d\n",
          memFrame[indx].next, memFrame[indx].prev);
}

void dump_memoryframe_info ()
{ int i;
		
  printf ("******************** Memory Frame Metadata\n");
  printf ("Memory frame head/tail: %d/%d\n", freeFhead, freeFtail);
  for (i=OSpages; i<numFrames; i++)
  { printf ("Frame %d: ", i); print_one_frameinfo (i); }
  dump_free_list ();
}

void  update_frame_info (findex, pid, page)
int findex, pid, page;
{ 
  // update the metadata of a frame, need to consider different update scenarios
  // need this function also becuase loader also needs to update memFrame fields
  // while it is better to not to expose memFrame fields externally
	if(pid!=idlePid){	
	if(findex==freeFhead && findex!=freeFtail){                     
                  freeFhead=memFrame[findex].next; 
		}
	else if(findex!=freeFhead && findex==freeFtail){ 
		freeFtail=memFrame[findex].prev;
		}
	else {	freeFtail=-1; 
		freeFhead=-1;
		}
	}
	      memFrame[findex].age = highestAge;
	      memFrame[findex].dirty = cleanFrame;
	      memFrame[findex].free = usedFrame;
	      memFrame[findex].pid = pid;
	      memFrame[findex].page = page;
	      memFrame[findex].next =-1;
	      memFrame[findex].prev =-1;
	
}

// should write dirty frames to disk and remove them from process page table
// but we delay updates till the actual swap (page_fault_handler)
// unless frames are from the terminated process (status = nullPage)
// so, the process can continue using the page, till actual swap
void addto_free_frame (int findex, int status)
 {
  	  
        printf("Added free frame = %d\n",findex);  
          if(status==nullPage){
              memFrame[findex].age = zeroAge;
	      memFrame[findex].dirty = cleanFrame;
	      memFrame[findex].free = freeFrame;
	      memFrame[findex].pinned = nopinFrame;
	      memFrame[findex].pid = nullPid;
	      memFrame[findex].page = nullPage;
	      memFrame[findex].next =findex +1;
	      memFrame[findex].prev = findex-1;
	    }
	   else{
	      memFrame[findex].age = zeroAge;	      
	      memFrame[findex].free = freeFrame;
	      memFrame[findex].pinned = nopinFrame;
	    }
	if(freeFhead==-1 && freeFtail==-1){freeFhead=findex;freeFtail=findex;} 
	else if(freeFhead==-1 && freeFtail!=-1){ if(findex>freeFtail) {freeFhead=freeFtail;freeFtail=findex;} else freeFhead=findex;}	    
	else if(freeFhead!=-1 && freeFtail==-1) {if(findex<freeFhead) {freeFtail=freeFhead;freeFhead=findex;} else freeFtail=findex;}
	else{ if(findex>freeFtail) freeFtail= findex; if(findex<freeFhead) freeFhead= findex;}
}

int select_agest_frame ()
{ 
  // select a frame with the lowest age 
  // if there are multiple frames with the same lowest age, then choose the one
  // that is not dirty
	int i, counter=0;
	int Agest_Frame;
	int Lowest_Age = highestAge;
	for (i=OSpages; i<numFrames; i++)
	 { 
	   if(memFrame[i].age<Lowest_Age) 
            {
	     Lowest_Age=memFrame[i].age;
	     }
         }
	for (i=OSpages; i<numFrames; i++)
	 { 
	   if(memFrame[i].age == Lowest_Age && memFrame[i].dirty == cleanFrame) 
             {
	     counter+=1;
	     if(counter==1){
	     Agest_Frame=i;
		}
	     else{	     
	     addto_free_frame (i, memFrame[i].page);
	     }	    
          }
	}
       if(counter==0){
	for (i=OSpages; i<numFrames; i++)
	 { 	   
	    if(memFrame[i].age == Lowest_Age && memFrame[i].dirty != cleanFrame) 
             {
	     Agest_Frame=i;	     
	     }
          }
	}
	printf("Selected agest frame = %d, age %x, dirty %d\n",Agest_Frame,memFrame[Agest_Frame].age,memFrame[Agest_Frame].dirty);
	return Agest_Frame;	
 }

int get_free_frame ()
{ 
// get a free frame from the head of the free list 
// if there is no free frame, then get one frame with the lowest age
// this func always returns a frame, either from free list or get one with lowest age
	int i;
	int Ret_Free;
	for (i = OSpages;  i < numFrames; i++) {
		if(memFrame[i].free == freeFrame){
			Ret_Free=i;
			printf("Got free frame = %d\n",Ret_Free);
	                dump_memoryframe_info ();
		      return Ret_Free;
		}
	}
	Ret_Free=select_agest_frame();
	printf("Got free frame = %d\n",Ret_Free);
	dump_memoryframe_info ();
	return Ret_Free;
} 

void initialize_memory ()
{ int i;

  // create memory + create page frame array memFrame 
  Memory = (mType *) malloc (numFrames*pageSize*sizeof(mType));
  memFrame = (FrameStruct *) malloc (numFrames*sizeof(FrameStruct));

  // compute #bits for page offset, set pagenumShift and pageoffsetMask
  // *** ADD CODE
	pagenumShift = log2(pageSize);//log(2);
  	pageoffsetMask = (pageSize*numFrames)-1;

  // initialize OS pages
  for (i=0; i<OSpages; i++)
  { memFrame[i].age = zeroAge;
    memFrame[i].dirty = cleanFrame;
    memFrame[i].free = usedFrame;
    memFrame[i].pinned = pinnedFrame;
    memFrame[i].pid = osPid;
  }
  // initilize the remaining pages, also put them in free list
  // *** ADD CODE
	for (i=OSpages; i<numFrames; i++)
	 { 
              memFrame[i].age = zeroAge;
	      memFrame[i].dirty = cleanFrame;
	      memFrame[i].free = freeFrame;
	      memFrame[i].pinned = nopinFrame;
	      memFrame[i].pid = nullPid;
	      memFrame[i].page = nullPage;
	      memFrame[i].next = i+1;
	      memFrame[i].prev = i-1;
	    }
	 freeFhead=OSpages;
         freeFtail=numFrames-1;	
	 memFrame[OSpages].prev = -1;
	 memFrame[numFrames-1].next = -1;
}

//==========================================
// process page table manamgement
//==========================================

void init_process_pagetable (int pid)
{ int i;

  PCB[pid]->PTptr = (int *) malloc (addrSize*maxPpages);
  for (i=0; i<maxPpages; i++) PCB[pid]->PTptr[i] = nullPage;
}

// frame can be normal frame number or nullPage, diskPage
void update_process_pagetable (pid, page, frame)
int pid, page, frame;
{ 
  // update the page table entry for process pid to point to the frame
  // or point to disk or null
	PCB[pid]->PTptr[page] = frame;
	printf("PT update for (%d, %d) to %d\n",pid,page,frame);
}

int free_process_memory (int pid)
{ 
  // free the memory frames for a terminated process
  // some frames may have already been freed, but still in process pagetable
	int i, counter=0;
	printf("Free frames allocated to process %d\n",pid);
	for (i=0; i<maxPpages; i++){
	   if(PCB[pid]->PTptr[i] != nullPage && PCB[pid]->PTptr[i] !=diskPage)
                  {
		       addto_free_frame(PCB[pid]->PTptr[i], nullPage);
         	  }
	   if(PCB[pid]->PTptr[i] != nullPage)
                  {				
		       PCB[pid]->PTptr[i] = nullPage;
		  }	   
          }
	
}


void load_into_memory(int pid, int page, int maxpageload)
{	
	int out_pid=nullPid, out_page=nullPage;
	int frame = get_free_frame();
	int addr = frame*pageSize;
 	if(memFrame[frame].dirty == dirtyFrame){ 
		out_pid=memFrame[frame].pid; out_page=memFrame[frame].page;
		update_frame_info (frame, memFrame[frame].pid, memFrame[frame].page);
		update_process_pagetable (memFrame[frame].pid, memFrame[frame].page, diskPage);
		insert_swapQ(memFrame[frame].pid, memFrame[frame].page, &Memory[addr], actWrite, Nothing);		
	   } 
	else if(memFrame[frame].pid != idlePid && memFrame[frame].pid != nullPid){ 
                out_pid=memFrame[frame].pid; out_page=memFrame[frame].page;		
		update_process_pagetable (memFrame[frame].pid, memFrame[frame].page, diskPage);		
	    }	
	 update_frame_info (frame, pid, page);
	 update_process_pagetable (pid, page, frame);
	 if(page<maxpageload-1) insert_swapQ(pid, page, &Memory[addr], actRead, Nothing);
	 else insert_swapQ(pid, page, &Memory[addr], actRead, toReady);	
	printf("Swap_in: in=(%d,%d,%x), out=(%d,%d,%x), m=%x\n",pid,page,&Memory[addr],out_pid,out_page,&Memory[addr],&Memory[0]);
}



void dump_process_pagetable (int pid)
{ 
  // print page table entries of process pid
	printf("************ Page Table Dump for Process %d\n",pid);	
	int i,counter=0;
	for (i=0; i<maxPpages; i++){
		printf("%d, ",PCB[pid]->PTptr[i]);
                counter++;
		if(counter%8==0){printf("\n");};
	}
	printf("\n");
}

void dump_process_memory (int pid)
{ 
  // print out the memory content for process pid
	int i;
	
	if(pid>idlePid){
	printf("************ Memory Dump for Process %d\n",pid);
	for (i=0; i<maxPpages; i++){
	       printf("***P/F:%d,%d:",i,PCB[pid]->PTptr[i]);
			if(PCB[pid]->PTptr[i] != nullPage && PCB[pid]->PTptr[i] !=diskPage){
					print_one_frameinfo (PCB[pid]->PTptr[i]);
					dump_one_frame(PCB[pid]->PTptr[i]);
			}
			else if (PCB[pid]->PTptr[i] != nullPage && PCB[pid]->PTptr[i] == diskPage){					
                                       dump_process_swap_page(pid,i);		
		        }
		printf("\n");
           }
	}
}

//==========================================
// the major functions for paging, invoked externally
//==========================================

#define sendtoReady 1  // has to be the same as those in swap.c
#define notReady 0   
#define actRead 0   
#define actWrite 1

void page_fault_handler ()
{ 
  // handle page fault
  // obtain a free frame or get a frame with the lowest age
  // if the frame is dirty, insert a write request to swapQ 
  // insert a read request to swapQ to bring the new page to this frame
  // update the frame metadata and the page tables of the involved processes
	int out_pid, out_page,in_pid,in_page;
	int frm = get_free_frame();	
	int addr = frm*pageSize;
	///Write Part........Cases	
	if(memFrame[frm].dirty == dirtyFrame){ 
		out_pid=memFrame[frm].pid; out_page=memFrame[frm].page; 		
		update_frame_info (frm, memFrame[frm].pid, memFrame[frm].page);
		update_process_pagetable (memFrame[frm].pid, memFrame[frm].page, diskPage);
		insert_swapQ(memFrame[frm].pid, memFrame[frm].page, &Memory[addr], actWrite, Nothing);		
	   } 
	else if(memFrame[frm].pid != idlePid && memFrame[frm].pid != nullPid){ 
                out_pid=memFrame[frm].pid; out_page=memFrame[frm].page;		
		update_process_pagetable (memFrame[frm].pid, memFrame[frm].page, diskPage);		
	    } 
	///Read Part........Cases		
	   if(Data_Exception==55){ 
		in_pid=CPU.Pid; in_page=(CPU.IRoperand/pageSize); 	
		printf("Page Fault Handler: pid/page=(%d,%d)\n",CPU.Pid, (CPU.IRoperand/pageSize));
		PCB[CPU.Pid]->numPF +=1;
		update_frame_info (frm, CPU.Pid, (CPU.IRoperand/pageSize));
		update_process_pagetable (CPU.Pid, (CPU.IRoperand/pageSize), frm);
		insert_swapQ(CPU.Pid, (CPU.IRoperand/pageSize), &Memory[addr], actRead, toReady);
		Data_Exception=0;
		}
	   else{
		in_pid=CPU.Pid; in_page=(CPU.PC/pageSize); 
		printf("Page Fault Handler: pid/page=(%d,%d)\n",CPU.Pid, (CPU.PC/pageSize));
		PCB[CPU.Pid]->numPF +=1;
		update_frame_info (frm, CPU.Pid, (CPU.PC/pageSize));
		update_process_pagetable (CPU.Pid,(CPU.PC/pageSize), frm);
		insert_swapQ(CPU.Pid, (CPU.PC/pageSize), &Memory[addr], actRead, toReady);
		}	
  	printf("Swap_in: in=(%d,%d,%x), out=(%d,%d,%x), m=%x\n",in_pid,in_page,&Memory[addr],out_pid,out_page,&Memory[addr],&Memory[0]);
}

// scan the memory and update the age field of each frame
void memory_agescan ()
{   	int i,counter=0;
	for (i=OSpages; i<numFrames; i++)
	 { 
            memFrame[i].age = (memFrame[i].age >>1);
	
	    if (memFrame[i].age == zeroAge&&memFrame[i].free !=freeFrame)
	      { 
		addto_free_frame(i, pendingPage);
	      }
	  }

}

void initialize_memory_manager ()
{ 
  // initialize memory and add page scan event request
	initialize_memory();
	add_timer (periodAgeScan, osPid, ageInterrupt, periodAgeScan);
}

