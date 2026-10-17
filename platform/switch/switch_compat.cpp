/**************************************************************************/
/*  switch_compat.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// POSIX bits that newlib/libnx don't provide, used by drivers/unix.

#include "switch_wrapper.h"

#include <dlfcn.h>
#include <ifaddrs.h>
#include <syslog.h>

#include <arpa/inet.h>
#include <malloc.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

int getifaddrs(struct ifaddrs **r_ifap) {
	*r_ifap = nullptr;

	// gethostid() returns the console's current IPv4 address (network byte order) through nifm.
	long host_id = gethostid();
	if (host_id == 0 || host_id == INADDR_LOOPBACK) {
		return 0;
	}

	struct Storage {
		struct ifaddrs ifa;
		struct sockaddr_in addr;
		struct sockaddr_in netmask;
		char name[8];
	};
	Storage *storage = (Storage *)calloc(1, sizeof(Storage));
	if (!storage) {
		return -1;
	}

	strcpy(storage->name, "wlan0");
	storage->addr.sin_family = AF_INET;
	storage->addr.sin_addr.s_addr = (in_addr_t)host_id;
	storage->netmask.sin_family = AF_INET;
	storage->netmask.sin_addr.s_addr = htonl(0xFFFFFF00);

	u32 subnet_mask = 0;
	if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
		u32 ip = 0, gateway = 0, dns1 = 0, dns2 = 0;
		if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip, &subnet_mask, &gateway, &dns1, &dns2)) && subnet_mask != 0) {
			storage->netmask.sin_addr.s_addr = subnet_mask;
		}
		nifmExit();
	}

	storage->ifa.ifa_name = storage->name;
	storage->ifa.ifa_addr = (struct sockaddr *)&storage->addr;
	storage->ifa.ifa_netmask = (struct sockaddr *)&storage->netmask;
	*r_ifap = &storage->ifa;
	return 0;
}

void freeifaddrs(struct ifaddrs *p_ifa) {
	// Single allocation, the ifaddrs struct is its first member.
	free(p_ifa);
}

unsigned int if_nametoindex(const char *p_name) {
	// getifaddrs() only ever reports one interface.
	return 1;
}

void *dlopen(const char *p_path, int p_mode) {
	return nullptr;
}

void *dlsym(void *p_handle, const char *p_symbol) {
	return nullptr;
}

int dlclose(void *p_handle) {
	return -1;
}

char *dlerror(void) {
	return (char *)"Dynamic libraries are not supported on Horizon.";
}

void openlog(const char *p_ident, int p_option, int p_facility) {
}

void vsyslog(int p_priority, const char *p_format, va_list p_args) {
	vfprintf(stderr, p_format, p_args);
	fputc('\n', stderr);
}

void syslog(int p_priority, const char *p_format, ...) {
	va_list args;
	va_start(args, p_format);
	vsyslog(p_priority, p_format, args);
	va_end(args);
}

void closelog(void) {
}

// No processes, pipes or signals on Horizon. OS_Switch overrides the OS_Unix methods that
// would use these; the stubs only satisfy the linker.

int execvp(const char *p_file, char *const p_argv[]) {
	errno = ENOSYS;
	return -1;
}

pid_t waitpid(pid_t p_pid, int *r_status, int p_options) {
	errno = ECHILD;
	return -1;
}

pid_t setsid(void) {
	errno = ENOSYS;
	return -1;
}

int pipe(int r_fds[2]) {
	errno = ENOSYS;
	return -1;
}

FILE *popen(const char *p_command, const char *p_mode) {
	errno = ENOSYS;
	return nullptr;
}

int pclose(FILE *p_stream) {
	errno = ECHILD;
	return -1;
}

int mkfifo(const char *p_path, mode_t p_mode) {
	errno = ENOSYS;
	return -1;
}

int sigaction(int p_sig, const struct sigaction *p_act, struct sigaction *r_oact) {
	// Accept and ignore: there's no way to receive SIGINT on Horizon anyway.
	if (r_oact) {
		memset(r_oact, 0, sizeof(struct sigaction));
	}
	return 0;
}

int posix_memalign(void **r_ptr, size_t p_alignment, size_t p_size) {
	void *ptr = memalign(p_alignment, p_size);
	if (!ptr) {
		return ENOMEM;
	}
	*r_ptr = ptr;
	return 0;
}

} // extern "C"
