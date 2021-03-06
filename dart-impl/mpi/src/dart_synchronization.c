/**
 *  \file  dart_synchronization.c
 *
 *  Synchronization operations.
 */

#include <dash/dart/base/logging.h>
#include <dash/dart/base/assert.h>
#include <dash/dart/base/mutex.h>

#include <dash/dart/if/dart_types.h>
#include <dash/dart/if/dart_globmem.h>
#include <dash/dart/if/dart_team_group.h>
#include <dash/dart/if/dart_communication.h>
#include <dash/dart/if/dart_synchronization.h>

#include <dash/dart/mpi/dart_team_private.h>
#include <dash/dart/mpi/dart_mem.h>
#include <dash/dart/mpi/dart_globmem_priv.h>
#include <dash/dart/mpi/dart_segment.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>


struct dart_lock_struct
{
  /**
   * Global memory storing the unit at the tail of lock queue.
   * Stored in team-unit 0 by default.
   */
  dart_gptr_t  gptr_tail;
  /**
   * Pointer to the current unit's successor in the waiting list,
   * to which we send a release message.
   */
  dart_gptr_t  gptr_list;
  /**
   * Local mutex to ensure mutual exclusion between threads.
   */
  dart_mutex_t mutex;
  dart_team_t teamid;
  /** Whether this unit has acquired the lock. */
  int32_t is_acquired;
};

dart_ret_t dart_team_lock_init(dart_team_t teamid, dart_lock_t* lock)
{
  int ret;
  dart_gptr_t gptr_tail;
  dart_gptr_t gptr_list;
  dart_team_unit_t unitid;

  *lock = NULL;


  dart_team_data_t *team_data = dart_adapt_teamlist_get(teamid);
  if (team_data == NULL) {
    return DART_ERR_INVAL;
  }

  dart_team_myid(teamid, &unitid);


  /* Unit 0 is the process holding the gptr_tail by default. */
  if (unitid.id == 0) {
    int32_t *tail_ptr;
    ret = dart_memalloc(1, DART_TYPE_INT, &gptr_tail);
    if (ret != DART_OK) {
      DART_LOG_ERROR("%s: Failed to allocate global memory!", __FUNCTION__);
      return ret;
    }
    DART_ASSERT_RETURNS(
      dart_gptr_getaddr(gptr_tail, (void*)&tail_ptr),
      DART_OK);

    /* Local store is safe and effective followed by the sync call. */
    *tail_ptr = -1;
    MPI_Win_sync (dart_win_local_alloc);
  }

  /* Create a global memory region across the team.
   * Every local memory segment holds the next unit
   * waiting on the lock. */
  ret = dart_team_memalloc_aligned(teamid, 1, DART_TYPE_INT, &gptr_list);
  if (ret != DART_OK) {
    DART_LOG_ERROR("%s: Failed to allocate global memory!", __FUNCTION__);
    return ret;
  }

  int32_t *list_ptr;
  MPI_Win win = team_data->window; // window object used for atomic operations

  dart_gptr_setunit(&gptr_list, unitid);
  dart_gptr_getaddr(gptr_list, (void*)&list_ptr);
  *list_ptr = -1;
  MPI_Win_sync(win);

  // communicate tail pointer
  ret = dart_bcast(
    &gptr_tail,
    sizeof(dart_gptr_t),
    DART_TYPE_BYTE,
    DART_TEAM_UNIT_ID(0),
    teamid);
  if (ret != DART_OK) {
    DART_LOG_ERROR("%s: Failed to broadcast lock information!", __FUNCTION__);
    return ret;
  }


  *lock = malloc(sizeof(struct dart_lock_struct));
  (*lock)->gptr_tail   = gptr_tail;
  (*lock)->gptr_list   = gptr_list;
  (*lock)->teamid      = teamid;
  (*lock)->is_acquired = 0;
  DART_ASSERT_RETURNS(
    dart__base__mutex_init_recursive(&(*lock)->mutex),
    DART_OK);

  DART_LOG_DEBUG("dart_team_lock_init: INIT - done");

  return DART_OK;
}

dart_ret_t dart_lock_acquire(dart_lock_t lock)
{
  /* lock the local mutex and keep it until the global lock is released */
  DART_ASSERT_RETURNS(dart__base__mutex_lock(&lock->mutex), DART_OK);

  if (lock->is_acquired == 1)
  {
    DART_LOG_ERROR("dart_lock_acquire: LOCK has already been acquired\n");
    DART_ASSERT_RETURNS(dart__base__mutex_unlock(&lock->mutex), DART_OK);
    return DART_ERR_INVAL;
  }

  dart_team_data_t *team_data = dart_adapt_teamlist_get(lock->teamid);
  if (team_data == NULL) {
    DART_LOG_ERROR("dart_lock_acquire ! failed: Unknown team %i!",
                   lock->teamid);
    DART_ASSERT_RETURNS(dart__base__mutex_unlock(&lock->mutex), DART_OK);
    return DART_ERR_INVAL;
  }

  dart_gptr_t gptr_tail = lock->gptr_tail;
  dart_gptr_t gptr_list = lock->gptr_list;

  uint64_t    tail_offset = gptr_tail.addr_or_offs.offset;
  dart_unit_t tail_unit   = gptr_tail.unitid;

  dart_team_unit_t unitid;
  dart_team_myid(lock->teamid, &unitid);

  int32_t predecessor;

  /* Fetch the current unit's tail and make this unit the new tail */
  DART_LOG_TRACE(
    "dart_lock_acquire: MPI_Fetch_and_op to set tail to unit %i on "
    "tail_unit %i with offset %lu",
    unitid.id, tail_unit, tail_offset);
  DART_ASSERT_RETURNS(
    MPI_Fetch_and_op(
      &unitid.id,
      &predecessor,
      MPI_INT32_T,
      tail_unit,
      tail_offset,
      MPI_REPLACE,
      dart_win_local_alloc),
    MPI_SUCCESS);
  DART_ASSERT_RETURNS(
      MPI_Win_flush(tail_unit, dart_win_local_alloc),
      MPI_SUCCESS);

  DART_LOG_TRACE("dart_lock_acquire: predecessor: %i unitid.id: %i",
    predecessor, unitid.id);

  /* If there was a previous tail (predecessor), update the previous tail's
   * next pointer with unitid and wait for notification from its predecessor.
   */
  if (predecessor != -1) {
    int32_t    result;
    MPI_Win    win;
    MPI_Status status;
    int16_t    seg_id = gptr_list.segid;
    MPI_Aint   disp_list;

    DART_ASSERT_RETURNS(
      dart_segment_get_disp(
        &team_data->segdata,
        seg_id,
        DART_TEAM_UNIT_ID(predecessor),
        &disp_list),
      DART_OK);
    win = team_data->window;

    /* Atomicity: Update its predecessor's next pointer */
    DART_ASSERT_RETURNS(
      MPI_Fetch_and_op(
        &unitid.id,
        &result,
        MPI_INT32_T,
        predecessor,
        disp_list,
        MPI_REPLACE,
        win),
      MPI_SUCCESS);

    DART_ASSERT_RETURNS(
      MPI_Win_flush(predecessor, win),
      DART_OK);

    /* Waiting for notification from its predecessor*/
    DART_LOG_DEBUG("dart_lock_acquire: waiting for notification from "
                   "%d in team %d",
                   predecessor, lock->teamid);

    MPI_Recv(NULL, 0, MPI_INT, predecessor, 0, team_data->comm,
        &status);
  }

  DART_LOG_DEBUG("dart_lock_acquire: lock acquired in team %d", lock->teamid);
  lock->is_acquired = 1;
  return DART_OK;
}

dart_ret_t dart_lock_try_acquire(dart_lock_t lock, int32_t *is_acquired)
{
  if (dart__base__mutex_trylock(&lock->mutex) != DART_OK) {
    *is_acquired = 0;
    DART_LOG_DEBUG("dart_lock_try_acquire: LOCK held in another thread\n");
    return DART_OK;
  }

  if (lock->is_acquired == 1)
  {
    DART_LOG_ERROR("dart_lock_try_acquire: LOCK has already been acquired\n");
    *is_acquired = 1;
    /* we are using a recursive lock so give up this recursion */
    DART_ASSERT_RETURNS(dart__base__mutex_unlock(&lock->mutex), DART_OK);
    return DART_ERR_INVAL;
  }

  dart_team_unit_t unitid;
  dart_team_myid(lock->teamid, &unitid);

  int32_t result;
  int32_t compare = -1;


  dart_gptr_t gptr_tail   = lock->gptr_tail;
  dart_unit_t tail_unit   = gptr_tail.unitid;
  uint64_t    tail_offset = gptr_tail.addr_or_offs.offset;

  /* Atomicity: Check if the lock is available and claim it if it is. */
  DART_ASSERT_RETURNS(
    MPI_Compare_and_swap(
      &unitid.id,
      &compare,
      &result,
      MPI_INT32_T,
      tail_unit,
      tail_offset,
      dart_win_local_alloc),
    MPI_SUCCESS);
  DART_ASSERT_RETURNS(
    MPI_Win_flush (tail_unit, dart_win_local_alloc),
    MPI_SUCCESS);

  /* If the old predecessor was -1, we have claimed the lock,
   * otherwise, do nothing. */
  if (result == -1)
  {
    lock->is_acquired = 1;
    *is_acquired = 1;
  } else {
    *is_acquired = 0;
    /* unlock the local mutex if we have not acqcuired the global lock */
    DART_ASSERT_RETURNS(dart__base__mutex_unlock(&lock->mutex), DART_OK);
  }

  DART_LOG_DEBUG("dart_lock_try_acquire: trylock %s in team %d",
                 (*is_acquired) ? "succeeded" : "failed",
                 lock->teamid);
  return DART_OK;
}

dart_ret_t dart_lock_release(dart_lock_t lock)
{
  if (lock->is_acquired == 0) {
    DART_LOG_ERROR("dart_lock_release: LOCK has not been acquired before\n");
    return DART_ERR_INVAL;
  }

  dart_gptr_t gptr_tail = lock->gptr_tail;
  dart_gptr_t gptr_list = lock->gptr_list;

  dart_team_data_t *team_data = dart_adapt_teamlist_get(lock->teamid);
  DART_ASSERT(team_data != NULL);

  uint64_t      offset_tail = gptr_tail.addr_or_offs.offset;
  dart_unit_t   tail        = gptr_tail.unitid;
  int32_t     * addr;
  DART_ASSERT_RETURNS(dart_gptr_getaddr(gptr_list, (void *)&addr), DART_OK);

  dart_team_unit_t unitid;
  dart_team_myid(lock->teamid, &unitid);

  int32_t result;
  int32_t reset = -1;
  MPI_Win win = team_data->window;

  /* Check if we are at the tail of this lock queue and reset the tail pointer
   * if we are. If that is the case we are done.
   * Otherwise, the reset fails and we need to send notification. */
  DART_ASSERT_RETURNS(
    MPI_Compare_and_swap(
      &reset,
      &unitid.id,
      &result,
      MPI_INT32_T,
      tail,
      offset_tail,
      dart_win_local_alloc),
    MPI_SUCCESS);
  DART_ASSERT_RETURNS(
    MPI_Win_flush(tail, dart_win_local_alloc),
    MPI_SUCCESS);

  if (result != unitid.id) {
    /* We are not at the tail of this lock queue. */
    int32_t  next;
    MPI_Aint disp_list;
    DART_LOG_DEBUG("dart_lock_release: waiting for next pointer "
                   "(tail = %d) in team %d",
                   result, (lock -> teamid));

    DART_ASSERT_RETURNS(dart_segment_get_disp(
          &team_data->segdata,
          gptr_list.segid,
          unitid,
          &disp_list), DART_OK);

    /* Wait for the update of our next pointer. */
    do {
      DART_ASSERT_RETURNS(
        MPI_Fetch_and_op(
          NULL,
          &next,
          MPI_INT,
          unitid.id,
          disp_list,
          MPI_NO_OP,
          win),
        MPI_SUCCESS);
      DART_ASSERT_RETURNS(
        MPI_Win_flush(unitid.id, win),
        MPI_SUCCESS);
    } while (next == -1);

    DART_LOG_DEBUG("dart_lock_release: notifying %d in team %d", next,
                   (lock->teamid));

    /* Notifying the next unit waiting on the lock queue. */
    MPI_Send(NULL, 0, MPI_INT, next, 0, team_data->comm);
    *addr = -1;
    MPI_Win_sync(win);
  }
  lock->is_acquired = 0;
  DART_ASSERT_RETURNS(dart__base__mutex_unlock(&lock->mutex), DART_OK);
  DART_LOG_DEBUG("dart_lock_release: release lock in team %d",
                 (lock -> teamid));
  return DART_OK;
}

dart_ret_t dart_team_lock_destroy(dart_lock_t* lock)
{
  dart_ret_t ret;
  dart_team_unit_t unitid;
  dart_gptr_t gptr_tail = (*lock)->gptr_tail;
  dart_gptr_t gptr_list = (*lock)->gptr_list;
  dart_team_t teamid    = (*lock)->teamid;

  dart_team_myid(teamid, &unitid);


  /* Unit 0 is the process holding the gptr_tail by default. */
  if (unitid.id == 0) {
    ret = dart_memfree(gptr_tail);
    if (ret != DART_OK) {
      DART_LOG_ERROR("Failed to free global mmeory");
      return ret;
    }
  }
  ret = dart_team_memfree(gptr_list);
  if (ret != DART_OK) {
    DART_LOG_ERROR("Failed to free global mmeory");
    return ret;
  }
  (*lock)->gptr_tail = DART_GPTR_NULL;
  (*lock)->gptr_list = DART_GPTR_NULL;
  (*lock)->teamid    = DART_TEAM_NULL;
  dart__base__mutex_destroy(&(*lock)->mutex);
  DART_LOG_DEBUG("dart_team_lock_free: done in team %d", teamid);
  free(*lock);
  *lock = NULL;
  return DART_OK;
}




