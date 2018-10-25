/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Handle generation of crash-dump reports.
 *
 * Version:	@(#)win_crashdump.c	1.0.9	2018/10/24
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Riley (Rai-chan),
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2017 Riley (Rai-chan).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../emu.h"
#include "../version.h"
#include "../plat.h"


#define ExceptionHandlerBufferSize (10240)


static PVOID	hExceptionHandler;
static char	*ExceptionHandlerBuffer,
		*CurrentBufferPointer;
static char	ExceptionHandlerFileName[1024];


static LONG CALLBACK
MakeCrashDump(PEXCEPTION_POINTERS ExceptionInfo)
{
    SYSTEMTIME SystemTime;
    HANDLE hDumpFile;
    char *BufPtr;
    DWORD i;

    /* Get current system date and time. */
    GetSystemTime(&SystemTime);

    /*
     * Win32-specific functions will be used wherever possible,
     * just in case the C stdlib-equivalents try to allocate
     * memory.
     * (The Win32-specific functions are generally just wrappers
     * over NT system calls anyway.)
     */
    if ((ExceptionInfo->ExceptionRecord->ExceptionCode >> 28) != 0xC) {
	/*
	 * ExceptionCode is not a fatal exception (high 4b of
	 * ntstatus = 0xC)  Not going to crash, let's not make
	 * a crash dump.
	 */
	return(EXCEPTION_CONTINUE_SEARCH);
    }

    /*
     * So, the program is about to crash. Oh no what do?
     * Let's create a crash dump file as a debugging-aid.
     *
     * First, get the path to the executable.
     */
    GetModuleFileName(NULL,ExceptionHandlerBuffer,ExceptionHandlerBufferSize);
    if (GetLastError() != ERROR_SUCCESS) {
	/* Could not get full path, create in current directory. */
	BufPtr = ExceptionHandlerBuffer;
    } else {
	/*
	 * Walk through the string backwards looking for the
	 * last backslash, so as to remove the "VARCem.exe"
	 * filename from the string.
	 */
	BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
	for (; BufPtr > ExceptionHandlerBuffer; BufPtr--) {
		if (BufPtr[0] == '\\') {
			/* Found backslash, terminate the string after it. */
			BufPtr[1] = 0;
			break;
		}
	}

	BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
    }

    /*
     * What would a good filename be?
     *
     * It should contain the current date and time so as
     * to be (hopefully!) unique.
     */
    plat_tempfile((wchar_t *)ExceptionHandlerBuffer, NULL, DUMP_FILE_EXT);
    wcstombs(ExceptionHandlerFileName, (wchar_t *)ExceptionHandlerBuffer,
					sizeof(ExceptionHandlerFileName));


    /* Now the filename is in the buffer, the file can be created. */
    hDumpFile = CreateFile(
	ExceptionHandlerFileName,	// The filename of the file to open.
	GENERIC_WRITE,			// The permissions to request.
	0,				// Make sure other processes can't
					// touch the crash dump at all
					// while it's open.
	NULL,				// Leave the security descriptor
					// undefined, it doesn't matter.
	OPEN_ALWAYS,			// Opens the file if it exists,
					// creates a new file if it doesn't.
	FILE_ATTRIBUTE_NORMAL,		// File attributes / etc don't matter.
	NULL);				// A template file is not being used.

    /* Check to make sure the file was actually created. */
    if (hDumpFile == INVALID_HANDLE_VALUE) {
	/* CreateFile() failed, so just do nothing more. */
	return(EXCEPTION_CONTINUE_SEARCH);
    }

    /*
     * Write the data we were passed out in a human-readable format.
     *
     * Get the name of the module where the exception occurred.
     */
    HMODULE hMods[1024];
    MODULEINFO modInfo;
    HMODULE ipModule = 0;
    DWORD cbNeeded, w;

    /* Try to get a list of all loaded modules. */
    if (EnumProcessModules(GetCurrentProcess(),
			   hMods, sizeof(hMods), &cbNeeded)) {
	/* Got it, now walk through all modules.. */
	for (w=0; w<(cbNeeded / sizeof(HMODULE)); w++) {
		/* For each module, get the module information. */
		GetModuleInformation(GetCurrentProcess(),
				     hMods[w], &modInfo, sizeof(MODULEINFO));
		/* If the exception address is in the range of this module.. */
		if ( ((char *)ExceptionInfo->ExceptionRecord->ExceptionAddress >= (char *)modInfo.lpBaseOfDll) &&
		   ((char *)ExceptionInfo->ExceptionRecord->ExceptionAddress < ((char *)modInfo.lpBaseOfDll + modInfo.SizeOfImage))) {
			/* ...this is the module we're looking for! */
			ipModule = hMods[w];
			break;
		}
	}
    }

    /* Start to put the crash-dump string into the buffer. */
    sprintf(ExceptionHandlerBuffer,
	"#\r\n# %s %s\r\n#\r\n"
	"# Crash on %d-%02d-%02d at %02d:%02d:%02d.%03d\r\n#\r\n"
	"\r\n"
	"Exception details:\r\n"
	" NTSTATUS code: 0x%08lx\r\n Address: 0x%p",
	emu_title, emu_fullversion,
	SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay,
	SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond,
	SystemTime.wMilliseconds,
	ExceptionInfo->ExceptionRecord->ExceptionCode,
	(void *)ExceptionInfo->ExceptionRecord->ExceptionAddress);

    /*
     * If we found the correct module, get the full path to
     * the module the exception occured at and include it.
     */
    BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
    if (ipModule != 0) {
	sprintf(BufPtr," [");
	GetModuleFileName(ipModule, &BufPtr[2],
			  ExceptionHandlerBufferSize - (int)strlen(ExceptionHandlerBuffer));
	if (GetLastError() == ERROR_SUCCESS) {
		BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
		sprintf(BufPtr,"]");
		BufPtr += 1;
	}
    }

    sprintf(BufPtr,
	"\r\nNumber of parameters: %lu\r\nException parameters: ",
	ExceptionInfo->ExceptionRecord->NumberParameters);

    for (i=0; i<ExceptionInfo->ExceptionRecord->NumberParameters; i++) {
	BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
	sprintf(BufPtr,"0x%p ",
	    (void *)ExceptionInfo->ExceptionRecord->ExceptionInformation[i]);
    }
    BufPtr = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer) - 1];

#if defined(__i386__) && !defined(__x86_64)
    PCONTEXT Registers = ExceptionInfo->ContextRecord;
	
    /* This binary is being compiled for x86, include a register dump. */
    sprintf(BufPtr,
	"\r\n\r\nRegister dump:\r\n\r\n"
	"EIP:0x%08lx\r\n"
	"EAX:0x%08lx EBX:0x%08lx ECX:0x%08lx EDX:0x%08lx\r\n"
	"EBP:0x%08lx ESP:0x%08lx ESI:0x%08lx EDI:0x%08lx\r\n\r\n",
	Registers->Eip,
	Registers->Eax, Registers->Ebx, Registers->Ecx, Registers->Edx,
	Registers->Ebp, Registers->Esp, Registers->Esi, Registers->Edi);
#else
    /* Register dump not supported by this architecture. */
    /* (MinGW headers seem to lack the x64 CONTEXT structure definition) */
    sprintf(BufPtr, "\r\n");
#endif
	
    /* Write the string to disk. */
    WriteFile(hDumpFile, ExceptionHandlerBuffer,
	      (int)strlen(ExceptionHandlerBuffer), NULL, NULL);

    /* Close the file. */
    CloseHandle(hDumpFile);

    /* Return, therefore causing the crash. */
    return(EXCEPTION_CONTINUE_SEARCH);
}


void
InitCrashDump(void)
{
    /*
     * An exception handler should not allocate memory,
     * so allocate 10kb for it to use if it gets called,
     * an amount which should be more than enough.
     */
    ExceptionHandlerBuffer = (char *)mem_alloc(ExceptionHandlerBufferSize);
    CurrentBufferPointer = ExceptionHandlerBuffer;

    /*
     * Register the exception handler.
     * Zero first argument means this exception handler gets
     * called last, therefore, crash dump is only made, when
     * a crash is going to happen.
     */
    hExceptionHandler = AddVectoredExceptionHandler(0, MakeCrashDump);
}
