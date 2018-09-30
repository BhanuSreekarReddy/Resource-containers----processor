//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for Processor Container
//
////////////////////////////////////////////////////////////////////////

// vim ts=4

#include "processor_container.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>



struct task 
{
    struct task_struct* thr;
    struct task* next;
};


struct container
{
    __u64 cid;
    int task_cnt;
    struct task*  task_list;
    struct container* next;
};

static struct container* ctr_list = NULL;
DEFINE_MUTEX(my_mutex);



void print_ctr(char* s){
struct container* c = NULL;
struct task* t = NULL;

c = ctr_list;
while(c != NULL){
    t = c->task_list;
    printk( "=>%s ctrid= %llu  : ",s, c->cid);
    while(t != NULL) {
        printk( "\t task pid = %d state %ld",t->thr->pid, t->thr->state);
        t = t->next;
    }
    c = c->next;
}

}

struct container* getContainerFromCid(__u64 cid){

    struct container* ctrNode ;
    if (ctr_list == NULL) {
        return NULL;
    }
    ctrNode = ctr_list;
    while(ctrNode != NULL){
        if ( ctrNode->cid == cid ) {
            //printk( "Container Found %llu \n",ctrNode->cid);    
            break;
        }
        ctrNode = ctrNode->next;
    }
    return ctrNode;
}

struct task* getNewTask(void) {
    struct task* tn = NULL;
    tn = (struct task*)kmalloc(sizeof(struct task), GFP_KERNEL);
    if ( tn == NULL){
        return NULL;
    }
    tn->next = NULL;
    tn->thr= current; 
    return tn;
}


struct container* getNewContainer(__u64 cid) {
    struct container* ctrNode = NULL;
    ctrNode = (struct container*)kmalloc(sizeof(struct container), GFP_KERNEL);
    if ( ctrNode == NULL){
        //printk( "Unable to allocate container, exiting  %llu \n", ctrCmd.cid);
        return NULL;
    }
    ctrNode->task_cnt = 1;
    ctrNode->cid = cid;
    ctrNode->next = NULL;
    ctrNode->task_list = NULL;
    return ctrNode;
}

void deleteContainerFromList(struct container* nodeToDel){

    struct container* temp = ctr_list;
    struct container* prev = NULL;

    if(temp == NULL){
        printk( "No Container exists \n");
        return ;    
    }
    
    if(nodeToDel == NULL){
        printk( "Cannot delete a NULL Node \n");
        return ;
    }
    if (nodeToDel == ctr_list ){
        if (nodeToDel->next == NULL){
            // only 1 ctr
            kfree(nodeToDel);
            ctr_list = NULL;
            return ;
        }else{
            ctr_list=nodeToDel->next;
            kfree(nodeToDel);
            return ;
        }
    }else{
        // del some container in middle or end
        while(temp->next!=NULL && temp != nodeToDel){
            prev=temp;
            temp=temp->next;
        }
        
        if(temp->next == NULL){
            // last node del it 
            prev->next = NULL;
            kfree(temp);
            return ;
        }
        else {
            prev->next = temp->next;
            kfree(temp);
            return ;
        }
    }
} 

/**
 * Delete the task in the container.
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), 
 */

int processor_container_delete(struct processor_container_cmd __user *user_cmd)
{
    struct processor_container_cmd ctrCmd;
    struct container* ctrNode = NULL;
    struct task* temp = NULL;
    struct task* prev = NULL;

    copy_from_user(&ctrCmd, user_cmd, sizeof(struct processor_container_cmd));

    printk( "try to take lock %d \n",current->pid);
    mutex_lock(&my_mutex);
    printk( "taken lock %d \n",current->pid);
    
    printk( "DESTROY CALLED Container with cid = %llu \n",ctrCmd.cid);

    // iterate container list and get the container with cid
    ctrNode = getContainerFromCid(ctrCmd.cid);

    if(ctrNode == NULL ){
        printk( "Container not found %llu \n",ctrNode->cid);
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }
      printk( "from_delete Container found %llu \n",ctrNode->cid);
    
    if(ctrNode->task_list == NULL ){
        printk( "Container has no tasks %llu \n",ctrNode->cid);
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }

    if (ctrNode->task_list->next == NULL){
        // only 1 task in container, delete task and container
        printk( "from_delete only 1 node %llu \n",ctrNode->cid);
        ctrNode->task_cnt =0;
        kfree(ctrNode->task_list);
        printk( "COMPLETELY DESTROY  Container %llu task_cnt %d  \n",ctrNode->cid, ctrNode->task_cnt);
        deleteContainerFromList(ctrNode);
        ctrNode=NULL;
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }else {
        // first task to be deleted
        printk( "from_delete delete a task %llu pid= %d\n",ctrNode->cid, current->pid);
        if (current == ctrNode->task_list->thr){
            printk( "from_delete first task to be del %llu \n",ctrNode->cid);
            
            temp = ctrNode->task_list;
            ctrNode->task_list = ctrNode->task_list->next;
            ctrNode->task_cnt -=1;
            kfree(temp);
        }else{
            printk( "from_delete not first be del %llu \n",ctrNode->cid);
            temp = ctrNode->task_list;
            prev = temp;
            while(temp != NULL && temp->thr != current){
                prev = temp;
                temp = temp->next;
            }
            printk( "from_delete came till here  %llu \n",ctrNode->cid);

            if (temp!= NULL && temp->next == NULL ){
                printk( "from_delete last node to be deleted  %llu \n",ctrNode->cid);
                // if temp is last node
                ctrNode->task_cnt -=1;
                prev->next = NULL;
                kfree(temp);
            }else if (temp!=NULL && temp->next != NULL){
                printk( "from_delete not the last node to be deleted  %llu \n",ctrNode->cid);
                ctrNode->task_cnt -=1;
                prev->next = temp->next;
                kfree(temp);
            }else{
                // can it even come here ??
        printk( "releasing lock %d",current->pid);
                mutex_unlock(&my_mutex);
                return 0;

            }
        }
        
    }
    //print_ctr("d");
    printk( "from_destroy going in if \n");
    if(ctrNode != NULL && ctrNode->task_list != NULL && ctrNode->task_cnt >= 1){
        printk( "from_destroy waking up %d  current is %d\n",ctrNode->task_list->thr->pid, current->pid);
        print_ctr("d>>>");
        temp = ctrNode->task_list;
        wake_up_process(temp->thr);
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }

        printk( "releasing lock %d",current->pid);
    mutex_unlock(&my_mutex);
    return 0;
}


/**
 * Create a task in the corresponding container.
 * external functions needed:
 * copy_from_user(), mutex_lock(), mutex_unlock(), set_current_state(), schedule()
 * 
 * external variables needed:
 * struct task_struct* current  
 */
int processor_container_create(struct processor_container_cmd __user *user_cmd)
{
    struct container* ctrNode = NULL;
    struct task* tn = NULL;
    struct task* temp = NULL;

    struct processor_container_cmd ctrCmd;

    printk( "try to take lock %d \n",current->pid);
    mutex_lock(&my_mutex);
    printk( "taken lock %d \n",current->pid);

    copy_from_user(&ctrCmd, user_cmd, sizeof(struct processor_container_cmd));
    
    ctrNode = getContainerFromCid(ctrCmd.cid);

    if (ctrNode != NULL) {

        //printk( "CONTAINER EXISTS  %llu \n", ctrCmd.cid);
        tn = getNewTask();
        if ( tn == NULL) {
        printk( "releasing lock %d",current->pid);
            mutex_unlock(&my_mutex);
            return 0;
        }    
        
            
        ctrNode->task_cnt += 1;
        
        for(temp = ctrNode->task_list; (temp != NULL) && (temp->next != NULL); temp = temp->next)
            ;;
        if(temp->next == NULL)
            temp->next = tn;

    } else{
        //printk( "Container Not found %llu, creating a container .....  \n", ctrCmd.cid);    

        ctrNode = getNewContainer(ctrCmd.cid);
        if (ctrNode == NULL) {
            //printk( "getNewContainer failed %llu.....  \n", ctrCmd.cid);    
        printk( "releasing lock %d",current->pid);
            mutex_unlock(&my_mutex);
            return 0;

        }
        //printk( " creating a task node for container .....  %llu \n", ctrCmd.cid);    
        
        tn = getNewTask();
        
        if ( tn == NULL) {
            //printk( "getNewTask failed %llu.....  \n", ctrCmd.cid);    
        printk( "releasing lock %d",current->pid);
            mutex_unlock(&my_mutex);
            return 0;
        }    
        ctrNode->task_list = tn;
        if(ctr_list == NULL) {
            //printk( " First Container Added %llu.....  \n", ctrCmd.cid);    
            ctr_list = ctrNode;
        }else{    
            //printk( " Additional Container Added %llu.....  \n", ctrCmd.cid);    
            ctrNode->next = ctr_list;
            ctr_list = ctrNode;
        }        
    }

    //print_ctr("c");
    if(ctrNode->task_list != NULL && ctrNode->task_cnt > 1 ) {
        set_current_state(TASK_INTERRUPTIBLE);
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        schedule();
    }else{
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
    }    
       return 0;
}

/**
 * switch to the next task in the next container
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), set_current_state(), schedule()
*/ 

int processor_container_switch(struct processor_container_cmd __user *user_cmd)
{
    struct task* temp1 = NULL;
    struct task* prev = NULL;
    struct container* ctrNode = NULL;
    printk( " try to take lock %d \n", current->pid);
    mutex_lock(&my_mutex);
    printk( "taken lock %d \n",current->pid);
    
    if (ctr_list == NULL) {
        printk( " No Container exists .. Do nothing  \n");
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;    
    }
    ctrNode = ctr_list;
    
    while( ctrNode != NULL ) {
        for (temp1 = ctrNode->task_list;  temp1 != NULL &&  temp1->thr !=current; temp1 = temp1->next) 
                ;;
    
        if(temp1 != NULL && (temp1->thr == current)){
            break;
        }
        ctrNode = ctrNode->next;
    }

    if (ctrNode == NULL){
        printk( "Container does not exist return from cswitch %d \n",current->pid);    
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;    
    }    


    if ( temp1 == NULL || ctrNode->task_list == NULL) {
        printk( "task is NULL or task_list in container is NULL \n");
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }


    if( ctrNode->task_cnt == 1 || ctrNode->task_list->next == NULL ){
        printk( "Single in Container %llu.. return from cswitch  \n", ctrNode->cid);
        // just a single task or no task, you own the container
        // enjoy
        printk( "releasing lock %d",current->pid);
        mutex_unlock(&my_mutex);
        return 0;
    }
    if(ctrNode != NULL && temp1 != NULL){
        if(temp1->next != NULL){
            printk( "1 Found ctr %llu and current threadid  %d..   \n", ctrNode->cid, temp1->thr->pid);
            if(ctrNode->task_list == temp1){
                printk( "2 At head move to tail   %d..   \n", temp1->thr->pid);
                // first node in list, remove and add to last 
                ctrNode->task_list = temp1->next;
                prev = ctrNode->task_list;
                while(prev->next != NULL){
                    prev = prev->next;
                }
                prev->next = temp1;
                temp1->next = NULL;
                printk( "3 Ctr %llu , now head %d 2nd last is %d and tail is  %d..  \n", ctrNode->cid, ctrNode->task_list->thr->pid, prev->thr->pid, prev->next->thr->pid);
                print_ctr("<><>X");
                // yield 
            }else {
               /* // some where in mid
                prev= ctrNode->task_list;
                while(prev != NULL && prev->next != temp1){
                    prev = prev->next;
                }
                if (prev == NULL && prev->next != temp1){
    printk( "releasing lock %d",current->pid);
                    mutex_unlock(&my_mutex);
                     return 0;
                }        
                if(prev != NULL && prev->next == temp1) {
                    prev->next = temp1->next;
                    while(prev->next != NULL){
                        prev=prev->next;
                    }
                    prev->next = temp1;
                    temp1->next = NULL;
                    printk( "4 Ctr %llu , now head %d tail is  %d..   \n", ctrNode->cid, ctrNode->task_list->thr->pid, prev->next->thr->pid);
                }else{
    printk( "releasing lock %d",current->pid);
                    mutex_unlock(&my_mutex);
                     return 0;
                     }
                     */
                     
                     
                    printk("Failed because current task is not at head \n");
                 
            }
        }
            
        printk( " Comes here  CtrId = %llu CtrNode->cnt %d Ctr head_of_list  %d \n", ctrNode->cid, ctrNode->task_cnt, ctrNode->task_list->thr->pid);
        print_ctr("<1SW1>");    
        temp1 = ctrNode->task_list;

        if(ctrNode != NULL && ctrNode->task_list != NULL && ctrNode->task_cnt > 1){

            printk( "----> CtrId = %llu Waking up      process %d \n", ctrNode->cid, temp1->thr->pid);
            printk( "+++++ CtrId = %llu making current process %d sleep \n", ctrNode->cid, current->pid);
            print_ctr("<2SW2>");    
    printk( "Current setting state to sleep  %d -- \n",current->pid);
            set_current_state(TASK_INTERRUPTIBLE);
    printk( "releasing lock %d by current\n",current->pid);
            mutex_unlock(&my_mutex);
            wake_up_process(temp1->thr);
            schedule();
            return 0;
        }else{
    printk( "releasing lock %d\n",current->pid);
            mutex_unlock(&my_mutex);
            return 0;
        }
    }
    printk( "releasing lock %d\n",current->pid);
    mutex_unlock(&my_mutex); 
    return 0;

}


/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int processor_container_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    switch (cmd)
    {
    case PCONTAINER_IOCTL_CSWITCH:
        return processor_container_switch((void __user *)arg);
    case PCONTAINER_IOCTL_CREATE:
        return processor_container_create((void __user *)arg);
    case PCONTAINER_IOCTL_DELETE:
        return processor_container_delete((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
