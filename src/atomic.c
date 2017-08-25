#include <sys/types.h>
#include <pthread.h>

#define LOCK_PREFIX "lock\n\t"

/* 
 * from kernel.
 */

void atomic_add( int * value, int inc_val) {

    asm volatile(
            LOCK_PREFIX
            "addl %1, %0      \n\t"
            : "+m"(*value)
            : "ir"(inc_val)
            : /*no clobber-list*/
            );

}

void atomic_sub( int * value, int dec_val) {

    asm volatile(
            LOCK_PREFIX
            "subl %1, %0      \n\t"
            : "+m"(*value)
            : "ir"(dec_val)
            : /*no clobber-list*/
            );

}

void atomic_inc( int * value) {

    asm volatile(
            LOCK_PREFIX
            "incl %0        \n\t"
            : "+m"(*value)
            : /* no input */
            : /*no clobber-list*/
            );

}

void atomic_dec( int * value) {

    asm volatile(
            LOCK_PREFIX
            "decl %0        \n\t"
            : "+m"(*value)
            : /* no input */
            : /*no clobber-list*/
            );

}

/*
 * Atomically decrements @value by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
int atomic_dec_and_test( int * value) {

    unsigned char c;
    asm volatile(
            LOCK_PREFIX
            "decl %0        \n\t"
            "sete %1        \n\t"
            : "+m"(*value), "=qm"(c)
            :             
            : "memory"
            );
    return (c != 0);

}

/*
 * Atomically increment @value by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
int atomic_inc_and_test( int * value) {

    unsigned char c;
    asm volatile(
            LOCK_PREFIX
            "decl %0        \n\t"
            "sete %1        \n\t"
            : "+m"(*value), "=qm"(c)
            :             
            : "memory"
            );
    return (c != 0);

}

int test_and_set(int *lock)
{
    int oldbit,bitnr=0;

    asm volatile (
            "lock               \n\t"
            "bts %1, (%2)       \n\t"
            "setc %%al          \n\t"
            "movzb %%al, %0     \n\t"
            : "=r" (oldbit)
            : "r" (bitnr), "r" (lock)
            : "memory", "cc", "%eax"
            );
    return oldbit;

}

int atomic_xchg(int *addr, int newval)
{   
   int result;

   asm volatile(
           LOCK_PREFIX
           "xchgl %0, %1" 
           : "+m" (*addr), "=a" (result) 
           : "1" (newval) 
           : "cc");
   return result;  
}
