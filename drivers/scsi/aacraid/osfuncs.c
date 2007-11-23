/*++
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *  osfuncs.c
 *
 * Abstract: Holds all of the O/S specific interface functions.
 *	
 --*/

#include "osheaders.h"

//static LKINFO_DECL(fsa_locks, "fsa_locks",0);

extern aac_options_t g_options;

OS_SOFTINTR g_idle_task = { 0, 0, 0, 0 };
struct wait_queue * g_wait_queue_ptr = NULL;
struct wait_queue g_wait;
int * OsIdleTask( void * data );


//-----------------------------------------------------------------------------
// MUTEX functions

/*----------------------------------------------------------------------------*/
OS_STATUS OsMutexInit( 
	OS_MUTEX *Mutex,
	OS_SPINLOCK_COOKIE Cookie )
/*----------------------------------------------------------------------------*/
{
	Mutex->wq_ptr = NULL;
	Mutex->lock_var = 0;
	return ( 0 );
}


/*----------------------------------------------------------------------------*/
void OsMutexDestroy( 
	OS_MUTEX *Mutex )
/*----------------------------------------------------------------------------*/
{
}


/*----------------------------------------------------------------------------*/
void OsMutexAcquire( 
	OS_MUTEX *Mutex )
/*----------------------------------------------------------------------------*/
{
	struct wait_queue wait = { current, NULL };
	unsigned long time_stamp;

	time_stamp = jiffies;

	if( test_and_set_bit( 0, &( Mutex->lock_var ) ) != 0 )
	{
		if( in_interrupt() )
			panic( "OsMutexAcquire going to sleep at interrupt time\n" );
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue( &( Mutex->wq_ptr ), &wait );
		while( test_and_set_bit( 0, &( Mutex->lock_var ) ) != 0 )
			schedule();
		remove_wait_queue( &( Mutex->wq_ptr ), &wait );
	}

	if( ( jiffies - 1 ) > time_stamp )
		cmn_err( CE_WARN, "Mutex %ld locked out for %ld ticks", 
			Mutex, jiffies - time_stamp );
}


/*----------------------------------------------------------------------------*/
void OsMutexRelease( 
	OS_MUTEX *Mutex )
/*----------------------------------------------------------------------------*/
{
	if( test_and_clear_bit( 0, &( Mutex->lock_var ) ) == 0 )
		cmn_err( CE_WARN, "OsMutexRelease: mutex not locked" );
	wake_up_interruptible( &( Mutex->wq_ptr ) );
}

// see man hierarchy(D5)
#define FSA_LOCK 1

//-----------------------------------------------------------------------------
// Spinlock functions

/*----------------------------------------------------------------------------*/
OS_SPINLOCK * OsSpinLockAlloc( void ) 
/*----------------------------------------------------------------------------*/
{
	OS_SPINLOCK *SpinLock;
	int i;

	SpinLock = ( OS_SPINLOCK * )kmalloc( sizeof( OS_SPINLOCK ), GFP_KERNEL );
	SpinLock->spin_lock = SPIN_LOCK_UNLOCKED;
	for( i = 0; i < 8; i++ )
		SpinLock->cpu_lock_count[ i ] = 0;
	return( SpinLock );
}


/*----------------------------------------------------------------------------*/
OS_STATUS OsSpinLockInit( 
	OS_SPINLOCK *SpinLock,
	OS_SPINLOCK_COOKIE Cookie )
/*----------------------------------------------------------------------------*/
{
	return( 0 );
}


/*----------------------------------------------------------------------------*/
void OsSpinLockDestroy( 
	OS_SPINLOCK *SpinLock )
/*----------------------------------------------------------------------------*/
{
	kfree( SpinLock );
	SpinLock = NULL;
}


/*----------------------------------------------------------------------------*/
void OsSpinLockAcquire( 
	OS_SPINLOCK *SpinLock )
/*----------------------------------------------------------------------------*/
{
	unsigned cpu_id, i;

	if( SpinLock )
	{
		/*
		if( OsSpinLockOwned( SpinLock ) )
		{
			SpinLock->lockout_count++;
			if( SpinLock->lockout_count > 200 )
			{
				cmn_err( CE_WARN, "spin lock #%ld: lockout_count high", SpinLock );
				SpinLock->lockout_count = 0;
			}
		}
		*/
		cpu_id = smp_processor_id();
		if( SpinLock->cpu_lock_count[ cpu_id ] )
			panic( "CPU %d trying to acquire lock again: lock count = %d\n", 
				cpu_id, SpinLock->cpu_lock_count[ cpu_id ] );
		/*
		for( i = 0; i < 8; i++ )
			if( SpinLock->cpu_lock_count[ i ] )
				panic( "Another CPU has the lock\n" );
		*/
		spin_lock_irqsave( &( SpinLock->spin_lock ), SpinLock->cpu_flag );
		SpinLock->cpu_lock_count[ cpu_id ]++;
	}
	else
		cmn_err( CE_WARN, "OsSpinLockAcquire: lock does not exist" );
}


/*----------------------------------------------------------------------------*/
void OsSpinLockRelease( 
	OS_SPINLOCK *SpinLock )
/*----------------------------------------------------------------------------*/
{
	if( SpinLock )
	{
		SpinLock->cpu_lock_count[ smp_processor_id() ]--;
		spin_unlock_irqrestore( &( SpinLock->spin_lock ), SpinLock->cpu_flag );
	}
	else
		cmn_err( CE_WARN, "OsSpinLockRelease: lock does not exist" );
}


/*----------------------------------------------------------------------------*/
int OsSpinLockOwned(
	OS_SPINLOCK *SpinLock )
/*----------------------------------------------------------------------------*/
{
#ifdef __SMP__
	if( SpinLock->spin_lock.lock != 0 )
		return( 1 );
	else
#endif
		return( 0 );
}


//-----------------------------------------------------------------------------
// CvLock functions

/*----------------------------------------------------------------------------*/
OS_CVLOCK *OsCvLockAlloc( void ) 
/*----------------------------------------------------------------------------*/
{
	OS_CVLOCK *cv_lock;

#ifdef CVLOCK_USE_SPINLOCK
	cv_lock = OsSpinLockAlloc(); 
#else
	cv_lock = ( OS_CVLOCK * )kmalloc( sizeof( OS_CVLOCK ), GFP_KERNEL );
	cv_lock->wq_ptr = NULL;
	cv_lock->lock_var = 0;
#endif

	return( cv_lock );
}


/*----------------------------------------------------------------------------*/
OS_STATUS OsCvLockInit( 
	OS_CVLOCK *cv_lock,
	OS_SPINLOCK_COOKIE Cookie )
/*----------------------------------------------------------------------------*/
{
	return ( 0 );
}


/*----------------------------------------------------------------------------*/
void OsCvLockDestroy( 
	OS_CVLOCK *cv_lock )
/*----------------------------------------------------------------------------*/
{
	if( cv_lock )
		kfree( cv_lock );
	cv_lock = NULL;
}


/*----------------------------------------------------------------------------*/
void OsCvLockAcquire( 
	OS_CVLOCK *cv_lock )
/*----------------------------------------------------------------------------*/
{
#ifdef CVLOCK_USE_SPINLOCK
	OsSpinLockAcquire( cv_lock );
#else
	OsMutexAcquire(	cv_lock );
#endif
}


/*----------------------------------------------------------------------------*/
void OsCvLockRelease( 
	OS_CVLOCK *cv_lock )
/*----------------------------------------------------------------------------*/
{
#ifdef CVLOCK_USE_SPINLOCK
	OsSpinLockRelease( cv_lock );
#else
	OsMutexRelease( cv_lock );
#endif
}


/*----------------------------------------------------------------------------*/
int OsCvLockOwned(
	OS_CVLOCK *cv_lock )
/*----------------------------------------------------------------------------*/
{
	return( 1 );
}


//-----------------------------------------------------------------------------
// Conditional variable functions

/*----------------------------------------------------------------------------*/
void OsCv_init ( 
	OS_CV_T *cv_ptr )
/*----------------------------------------------------------------------------*/
{
	cv_ptr->lock_var = 1;
	cv_ptr->wq_ptr = NULL;
}


/*----------------------------------------------------------------------------*/
void OsCv_destroy( 
	OS_CV_T  *cv_ptr )
/*----------------------------------------------------------------------------*/
{
}


/*----------------------------------------------------------------------------*/
void OsCv_wait( 
	OS_CV_T *cv_ptr, 
	OS_CVLOCK *cv_lock_ptr )
/*----------------------------------------------------------------------------*/
{
	struct wait_queue wait = { current, NULL };
	unsigned long flags;
	
	if( in_interrupt() )
		panic( "OsCv_wait going to sleep at interrupt time\n" );

	cv_ptr->type = TASK_UNINTERRUPTIBLE;
	current->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue( &( cv_ptr->wq_ptr ), &wait );

	OsCvLockRelease( cv_lock_ptr );
	schedule();

	while( test_and_set_bit( 0, &( cv_ptr->lock_var ) ) != 0 )
	{
		if( in_interrupt() )
			panic( "OsCv_wait going to sleep at interrupt time\n" );
		schedule();
	}

	remove_wait_queue( &( cv_ptr->wq_ptr ), &wait );
	
	OsCvLockAcquire( cv_lock_ptr );
}


/*----------------------------------------------------------------------------*/
int OsCv_wait_sig( 
	OS_CV_T *cv_ptr,
	OS_CVLOCK *cv_lock_ptr ) 
/*----------------------------------------------------------------------------*/
{
	struct wait_queue wait = { current, NULL };
	unsigned long flags;
	int signal_state = 1;
	
	if( in_interrupt() )
		panic( "OsCv_wait_sig going to sleep at interrupt time\n" );

	cv_ptr->type = TASK_INTERRUPTIBLE;
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue( &( cv_ptr->wq_ptr ), &wait );

	OsCvLockRelease( cv_lock_ptr );
	schedule();

	while( ( test_and_set_bit( 0, &( cv_ptr->lock_var ) ) != 0 ) && 
			( !signal_pending( current ) ) )
	{
		if( in_interrupt() )
			panic( "OsCv_wait_sig going to sleep at interrupt time\n" );
		schedule();
	}

	if( signal_pending( current ) )
		signal_state = 0;
	
	remove_wait_queue( &( cv_ptr->wq_ptr ), &wait );
	
	OsCvLockAcquire( cv_lock_ptr );
	return( signal_state );
}


/*----------------------------------------------------------------------------*/
void OsCv_signal( 
	OS_CV_T *cv_ptr )
/*----------------------------------------------------------------------------*/
{
	clear_bit( 0, &( cv_ptr->lock_var ) );
	if( cv_ptr->type == TASK_INTERRUPTIBLE )
		wake_up_interruptible( &( cv_ptr->wq_ptr ) );
	else
		wake_up( &( cv_ptr->wq_ptr ) );
}


//-----------------------------------------------------------------------------
// Deferred procedure call functions

// create a soft interrupt object
/*----------------------------------------------------------------------------*/
int OsSoftInterruptAdd( 
	OS_SOFTINTR **ptr,
	void * handler,
	void * data )
/*----------------------------------------------------------------------------*/
{
	OS_SOFTINTR *tmp_ptr;

	if( !( tmp_ptr = ( OS_SOFTINTR * )kmalloc( sizeof( OS_SOFTINTR ), GFP_KERNEL ) ) )
		return( -1 );
	tmp_ptr->routine = handler;
	tmp_ptr->data = data;
	tmp_ptr->next = NULL;
	tmp_ptr->sync = 0;

	*ptr = tmp_ptr; 

	return( 0 );
}

/*
	Use kernel_thread( ( int ( * )( void * ) )OsIdleTask, NULL, 0 ); to start
*/
/*----------------------------------------------------------------------------*/
int * OsIdleTask( void * data )
/*----------------------------------------------------------------------------*/
{
	struct wait_queue wait = { current, NULL };

	while( 1 )
	{
		//interruptible_sleep_on( &g_wait_queue_ptr );
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue( &g_wait_queue_ptr, &wait );
		schedule();
		remove_wait_queue( &g_wait_queue_ptr, &wait );
		wait.task =  current;
		wait.next = NULL;
	}
	return( NULL );
}


// dispatch a soft interrupt 
/*----------------------------------------------------------------------------*/
void OsSoftInterruptTrigger( 
	OS_SOFTINTR *soft_intr_ptr )
/*----------------------------------------------------------------------------*/
{
	// wake up a kernel thread
	//wake_up_interruptible( &g_wait_queue_ptr );

	// put in scheduler task queue
	//queue_task( soft_intr_ptr, &tq_scheduler );
	
	// call the completion routine directly
	soft_intr_ptr->routine( soft_intr_ptr->data );
}


// delete a soft interrupt object
/*----------------------------------------------------------------------------*/
void OsSoftInterruptRemove( 
	OS_SOFTINTR *arg )
/*----------------------------------------------------------------------------*/
{
	if( arg )
		kfree( arg );
	arg = NULL;
}


/*----------------------------------------------------------------------------*/
void OsSleep(unsigned time)		// in seconds
/*----------------------------------------------------------------------------*/
{
	current->state = TASK_UNINTERRUPTIBLE;	
	schedule_timeout(time*HZ);
}


