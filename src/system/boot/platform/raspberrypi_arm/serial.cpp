/*
 * Copyright 2004-2008, Axel D??rfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2009 Jonas Sundström, jonas@kirilla.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "serial.h"
#include "uart.h"

#include <boot/platform.h>
#include <arch/cpu.h>
#include <boot/stage2.h>

#include <string.h>


UartPL011* gLoaderUART;

static int32 sSerialEnabled = 0;
static char sBuffer[16384];
static uint32 sBufferPosition;


static void
serial_putc(char c)
{
	gLoaderUART->PutChar(c);
}


extern "C" void
serial_puts(const char* string, size_t size)
{
	if (sSerialEnabled <= 0)
		return;

	if (sBufferPosition + size < sizeof(sBuffer)) {
		memcpy(sBuffer + sBufferPosition, string, size);
		sBufferPosition += size;
	}

	while (size-- != 0) {
		char c = string[0];

		if (c == '\n') {
			serial_putc('\r');
			serial_putc('\n');
		} else if (c != '\r')
			serial_putc(c);

		string++;
	}
}


extern "C" void
serial_disable(void)
{
	sSerialEnabled--;
}


extern "C" void
serial_enable(void)
{
	sSerialEnabled++;
}


extern "C" void
serial_cleanup(void)
{
#warning IMPLEMENT serial_cleanup
}


extern "C" void
serial_init(void)
{
	gLoaderUART = new(nothrow) UartPL011(uart_base_debug());
	if (gLoaderUART == 0)
		return;

	gLoaderUART->InitEarly();
	gLoaderUART->InitPort(9600);

	serial_enable();

	serial_putc('!');
	serial_puts("SER INIT", 8);
}
