#include <stdio.h>
#include <stdlib.h>
#define FIFO_SIZE 20
#define PCB 10

unsigned char *ku_mmu_pmemory;//physical memory
int *ku_mmu_smemory;//swap space => 1(free), 0(charged)
int *ku_mmu_freelist;//free list => 1(free), 0(charged)
int ku_mmu_list_size, ku_mmu_ssize;//freelist's length, swap space's length
int ku_mmu_page_fifo[FIFO_SIZE];//array for page fifo
int ku_mmu_pcb_index[PCB];//array of pfn which page consists pcb 

struct ku_mmu_pcb{//pcb struct
    char pid;
    char pd_num; 
};

int ku_mmu_check_full(){//check if there is a free page for new page
    for(int i=1; i<ku_mmu_list_size;i++){//find free space
        if(ku_mmu_freelist[i]==1){
            ku_mmu_freelist[i]=0;//set free list to 0(charged)
            return i;//if there is, return free page number
        }
    }//if there isn't
    int outpage = ku_mmu_page_fifo[0];//find page number to swap out from fifo
    int free_swap_space;//index for free space in swap space
    for(int i=1;i<ku_mmu_ssize;i++){//
        if(ku_mmu_smemory[i]==1){
            free_swap_space = i;
            ku_mmu_smemory[i]=0;
            break;//find free swap space and set to 0(charged)
        }
    }
    for(int i=0;i<FIFO_SIZE-1;i++){//update fifo
        ku_mmu_page_fifo[i] = ku_mmu_page_fifo[i+1];
    }
    ku_mmu_page_fifo[FIFO_SIZE-1]=0;

    for(int i=0;i<ku_mmu_list_size * 4;i++){//find pfn that swaps out and change it's value to swap space offset
        if(ku_mmu_pmemory[i]==(outpage * 4 + 1)){//find pfn that equals to "outpage"
            ku_mmu_pmemory[i] = (free_swap_space * 2);//set its value to swap space offset
            break; 
        }
    }
    return outpage;//return outpage
}

void ku_mmu_put_fifo(int page){//add new pfn to fifo
    for(int i=0;i<FIFO_SIZE;i++){
        if(ku_mmu_page_fifo[i] == 0){
            ku_mmu_page_fifo[i] = page;
            break;
        }
    }
}

void ku_mmu_makepcb(char pid, int page_dirnum){//making pcb and saving it in physical memory
    struct ku_mmu_pcb npcb;
    npcb.pid = pid;//process id
    npcb.pd_num = (char)page_dirnum;//process page directory's page number

    int page_for_pcb = ku_mmu_check_full();//find free page for pcb
    //uses only half of the page
    ku_mmu_pmemory[page_for_pcb * 4] = npcb.pid;
    ku_mmu_pmemory[page_for_pcb * 4 + 1] = npcb.pd_num;

    for(int i=0;i<PCB;i++){//add pcb's page pfn to pcb index
        if(ku_mmu_pcb_index[i]==0){
            ku_mmu_pcb_index[i]=page_for_pcb;
            break;
        }
    }
}

struct ku_mmu_pcb ku_mmu_findpcb(char pid){//finding pcb with pid
    struct ku_mmu_pcb pcb;
    for(int i=0;i<PCB;i++){
        if(ku_mmu_pcb_index[i]==0)//the case that pid is new
            break;
        if(ku_mmu_pmemory[ku_mmu_pcb_index[i] * 4] == pid){//if there is already pid in physical memory
            pcb.pid = pid;
            pcb.pd_num = ku_mmu_pmemory[ku_mmu_pcb_index[i] * 4 + 1];
            return pcb;//return that information
        }
    }
    pcb.pid=0;
    pcb.pd_num=0;
    return pcb;//return pcb for new pid
}

void *ku_mmu_init(unsigned int mem_size, unsigned int swap_size){

    ku_mmu_pmemory = (unsigned char *)malloc(mem_size);//memory for physical memory
    ku_mmu_smemory = (int *)malloc(swap_size);//memory for swap space
    ku_mmu_freelist = (int *)malloc(mem_size);//memory for freelist

    if(ku_mmu_pmemory == NULL || ku_mmu_smemory == NULL || ku_mmu_freelist == NULL)
        return 0;//if any of them is not allocated

    ku_mmu_ssize = swap_size/4;//length of swap space array
    ku_mmu_list_size = mem_size/4;//length of free list array
    
    for(int i=0;i<mem_size;i++){//init physical memory with 0
        ku_mmu_pmemory[i]=0;
    }

    ku_mmu_smemory[0]=0;//first index of swap space is unusuable
    for(int i=1;i<ku_mmu_ssize;i++){
        ku_mmu_smemory[i]=1;//set all index to 1(free)
    }

    ku_mmu_freelist[0]=0;//init free list, page number 0 is unusable
    for(int i=1; i<(ku_mmu_list_size); i++){
        ku_mmu_freelist[i]=1;
    }

    for(int i=0;i<FIFO_SIZE;i++){//init fifo
        ku_mmu_page_fifo[i]=0;
    }

    for(int i=0;i<PCB;i++){//init pcb page number list
        ku_mmu_pcb_index[i]=0;
    }

    return ku_mmu_pmemory;
}

int ku_run_proc(char pid, struct ku_pte **ku_cr3){
    struct ku_mmu_pcb found = ku_mmu_findpcb(pid);//get pcb structure using pid
    if(found.pid != 0){//found
        *ku_cr3 = &ku_mmu_pmemory[found.pd_num * 4];//get page directory's address
        return 0;
    }else {//new pid
        int page_for_dir = ku_mmu_check_full();//get free page for new process page directory
        unsigned char *newpd;
        newpd=&ku_mmu_pmemory[page_for_dir * 4];//get page dir's address
        *ku_cr3=newpd;
        ku_mmu_makepcb(pid, page_for_dir);//make pcb structure and save it it physical memory
        return 0;
    }
    return -1;//if failed to do all the work
}

int ku_page_fault(char pid, char va){
    //새로운 process mapping
    int success = 0 ;//check if mapping succeed
    struct ku_mmu_pcb newpcb = ku_mmu_findpcb(pid);//find process's page dir address
    char *newpdbr = &ku_mmu_pmemory[newpcb.pd_num * 4];//page dir address

    char dir_offset = va/64;//pade directory's offset
    char mid_offset = (va%64)/16;//middle directory's offset
    char tab_offset = ((va%64)%16)/4;//table's offset

    unsigned char *pde = newpdbr + dir_offset;//accessing pde with address
    int mid_page, tab_page;//var for saving pde's & mde's value

    if(*pde==0){//nothing has been mapped yet
        
        int page_for_pmd = ku_mmu_check_full();//find empty page for middle
        *pde = page_for_pmd * 4 + 1;//set pde to 6bit frame number and present bit 1 total 8bit
        mid_page = page_for_pmd;
    
        int page_for_pt = ku_mmu_check_full();//find empty page for table
        ku_mmu_pmemory[mid_page * 4 + mid_offset] = page_for_pt * 4 + 1;//set mde to 6bit frame nubmer and present bit 1 
        tab_page = page_for_pt;
        
        int page_for_frame = ku_mmu_check_full();//find empty page for page frame
        ku_mmu_pmemory[tab_page * 4 + tab_offset] = page_for_frame * 4 + 1;//set pte to 6bit frame nubmer and present bit 1 
        ku_mmu_put_fifo(page_for_frame);//put this page in fifo
        success = 1;
           
    }else{//dir=>middle already mapped
        int mde_index = (*pde/4) * 4 + mid_offset;//find mde's index in physical memory
        if(ku_mmu_pmemory[mde_index] == 0){//middle=>table not mapped
            
            int page_for_table = ku_mmu_check_full();//find empty page for table
            ku_mmu_pmemory[mde_index] = page_for_table * 4 + 1;//set mde to 6bit frame nubmer and present bit 1 
            tab_page = page_for_table;

            int page_for_frame = ku_mmu_check_full();//find empty page for page frame
            ku_mmu_pmemory[tab_page * 4 + tab_offset] = page_for_frame * 4 + 1;//set pte to 6bit frame nubmer and present bit 1 
            ku_mmu_put_fifo(page_for_frame);//put this page in fifo
            success = 1;

        }else{//middle=>table mapped
            int pte_index = (ku_mmu_pmemory[mde_index]/4) * 4 + tab_offset;//find pte's index in physical memory
            if(ku_mmu_pmemory[pte_index] == 0){//table=>frame not mapped
                
                int page_for_frame = ku_mmu_check_full();//find new page for page frame
                ku_mmu_pmemory[pte_index] = page_for_frame * 4 + 1;//set pte to 6bit frame nubmer and present bit 1 
                ku_mmu_put_fifo(page_for_frame);//put this page in fifo
                success = 1;
                   
            }else if(ku_mmu_pmemory[pte_index]%2==0){//frame has been swapped out
                int swap_index = ku_mmu_pmemory[pte_index]/2;
                ku_mmu_smemory[swap_index]=1;//free swap space

                int page_for_frame = ku_mmu_check_full();//find new page for page frame
                ku_mmu_pmemory[pte_index]=page_for_frame * 4 + 1;//set pte to 6bit frame nubmer and present bit 1 

                ku_mmu_put_fifo(page_for_frame);//put this page in fifo
                success = 1;
            }
        }
    }
    if(success == 1)//if everything succeed
        return 0;
    return -1;
}
