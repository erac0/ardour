/*
 * Copyright (C) 2010-2014 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2015-2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "pbd/semutils.h"
#include "pbd/failed_constructor.h"

#ifdef USE_FUTEX_SEMAPHORE
#include <errno.h>
#include <linux/futex.h>
#include <sched.h>
#include <syscall.h>
#include <unistd.h>
#endif

using namespace PBD;

Semaphore::Semaphore (const char* name, int val)
{
#ifdef WINDOWS_SEMAPHORE
	(void) name; /* stop warning */
	if ((_sem = CreateSemaphore(NULL, val, 32767, NULL)) == NULL) {
		throw failed_constructor ();
	}

#elif __APPLE__
	if ((_sem = sem_open (name, O_CREAT, 0600, val)) == (sem_t*) SEM_FAILED) {
		throw failed_constructor ();
	}

	/* this semaphore does not exist for IPC */

	if (sem_unlink (name)) {
		throw failed_constructor ();
	}

#elif defined USE_FUTEX_SEMAPHORE
	(void)name; /* stop warning */
	_value = val;
#else
	(void) name; /* stop gcc warning on !Apple systems */

	if (sem_init (&_sem, 0, val)) {
		throw failed_constructor ();
	}
#endif
}

Semaphore::~Semaphore ()
{
#ifdef WINDOWS_SEMAPHORE
	CloseHandle(_sem);
#elif __APPLE__
	sem_close (ptr_to_sem());
#endif
}

#ifdef WINDOWS_SEMAPHORE

int
Semaphore::signal ()
{
	// non-zero on success, opposite to posix
	return !ReleaseSemaphore(_sem, 1, NULL);
}

int
Semaphore::wait ()
{
	DWORD result = 0;
	result = WaitForSingleObject(_sem, INFINITE);
	return (result == WAIT_OBJECT_0 ? 0 : -1);
}

int
Semaphore::reset ()
{
	int rv = -1;
	DWORD result;
	do {
		++rv;
		result = WaitForSingleObject(_sem, 0);
	} while (result == WAIT_OBJECT_0);
	return rv;
}

#elif defined USE_FUTEX_SEMAPHORE

int
Semaphore::signal ()
{
	if (std::atomic_fetch_add_explicit (&_value, 1, std::memory_order_relaxed) < 0) {
		while (syscall (__NR_futex, &_futex, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0) < 1) {
			sched_yield();
		}
	}
	return 0;
}

int
Semaphore::wait ()
{
	if (std::atomic_fetch_sub_explicit (&_value, 1, std::memory_order_relaxed) <= 0) {
		syscall(__NR_futex, &_futex, FUTEX_WAIT_PRIVATE, _futex, NULL, 0, 0);
	}
	return 0;
}

int
Semaphore::reset ()
{
	int value = _value;
	_value = 0;
	return value;
}

#endif
