#include "stdafx.h"
#include "Emu/Memory/Memory.h"
#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/SysCalls/SysCalls.h"

#include "Emu/CPU/CPUThreadManager.h"
#include "Emu/Cell/PPUThread.h"
#include "sleep_queue.h"
#include "sys_time.h"
#include "sys_lwmutex.h"

SysCallBase sys_lwmutex("sys_lwmutex");

void lwmutex_create(sys_lwmutex_t& lwmutex, bool recursive, u32 protocol, u64 name)
{
	std::shared_ptr<lwmutex_t> mutex(new lwmutex_t(protocol, name));

	lwmutex.lock_var.write_relaxed({ lwmutex::free, lwmutex::zero });
	lwmutex.attribute = protocol | (recursive ? SYS_SYNC_RECURSIVE : SYS_SYNC_NOT_RECURSIVE);
	lwmutex.recursive_count = 0;
	lwmutex.sleep_queue = Emu.GetIdManager().GetNewID(mutex, TYPE_LWMUTEX);
}

s32 _sys_lwmutex_create(vm::ptr<u32> lwmutex_id, u32 protocol, vm::ptr<sys_lwmutex_t> control, u32 arg4, u64 name, u32 arg6)
{
	sys_lwmutex.Warning("_sys_lwmutex_create(lwmutex_id=*0x%x, protocol=0x%x, control=*0x%x, arg4=0x%x, name=0x%llx, arg6=0x%x)", lwmutex_id, protocol, control, arg4, name, arg6);

	switch (protocol)
	{
	case SYS_SYNC_FIFO: break;
	case SYS_SYNC_RETRY: break;
	case SYS_SYNC_PRIORITY: break;
	default: sys_lwmutex.Error("_sys_lwmutex_create(): invalid protocol (0x%x)", protocol); return CELL_EINVAL;
	}

	if (arg4 != 0x80000001 || arg6)
	{
		sys_lwmutex.Error("_sys_lwmutex_create(): unknown parameters (arg4=0x%x, arg6=0x%x)", arg4, arg6);
	}

	std::shared_ptr<lwmutex_t> mutex(new lwmutex_t(protocol, name));

	*lwmutex_id = Emu.GetIdManager().GetNewID(mutex, TYPE_LWMUTEX);

	return CELL_OK;
}

s32 _sys_lwmutex_destroy(u32 lwmutex_id)
{
	sys_lwmutex.Warning("_sys_lwmutex_destroy(lwmutex_id=%d)", lwmutex_id);

	LV2_LOCK;

	std::shared_ptr<lwmutex_t> mutex;
	if (!Emu.GetIdManager().GetIDData(lwmutex_id, mutex))
	{
		return CELL_ESRCH;
	}

	if (mutex->waiters)
	{
		return CELL_EBUSY;
	}

	Emu.GetIdManager().RemoveID(lwmutex_id);

	return CELL_OK;
}

s32 _sys_lwmutex_lock(u32 lwmutex_id, u64 timeout)
{
	sys_lwmutex.Log("_sys_lwmutex_lock(lwmutex_id=%d, timeout=0x%llx)", lwmutex_id, timeout);

	const u64 start_time = get_system_time();

	LV2_LOCK;

	std::shared_ptr<lwmutex_t> mutex;
	if (!Emu.GetIdManager().GetIDData(lwmutex_id, mutex))
	{
		return CELL_ESRCH;
	}

	// protocol is ignored in current implementation
	mutex->waiters++; assert(mutex->waiters > 0);

	while (!mutex->signals)
	{
		if (timeout && get_system_time() - start_time > timeout)
		{
			mutex->waiters--; assert(mutex->waiters >= 0);
			return CELL_ETIMEDOUT;
		}

		if (Emu.IsStopped())
		{
			sys_lwmutex.Warning("_sys_lwmutex_lock(lwmutex_id=%d) aborted", lwmutex_id);
			return CELL_OK;
		}

		mutex->cv.wait_for(lv2_lock, std::chrono::milliseconds(1));
	}
	
	mutex->signals--;

	mutex->waiters--; assert(mutex->waiters >= 0);

	return CELL_OK;
}

s32 _sys_lwmutex_trylock(u32 lwmutex_id)
{
	sys_lwmutex.Log("_sys_lwmutex_trylock(lwmutex_id=%d)", lwmutex_id);

	LV2_LOCK;

	std::shared_ptr<lwmutex_t> mutex;
	if (!Emu.GetIdManager().GetIDData(lwmutex_id, mutex))
	{
		return CELL_ESRCH;
	}

	if (mutex->waiters || !mutex->signals)
	{
		return CELL_EBUSY;
	}

	mutex->signals--;

	return CELL_OK;
}

s32 _sys_lwmutex_unlock(u32 lwmutex_id)
{
	sys_lwmutex.Log("_sys_lwmutex_unlock(lwmutex_id=%d)", lwmutex_id);

	LV2_LOCK;

	std::shared_ptr<lwmutex_t> mutex;
	if (!Emu.GetIdManager().GetIDData(lwmutex_id, mutex))
	{
		return CELL_ESRCH;
	}

	mutex->signals++;
	mutex->cv.notify_one();

	return CELL_OK;
}
