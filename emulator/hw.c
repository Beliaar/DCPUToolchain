/**

	File:			hw.c

	Project:		DCPU-16 Tools
	Component:		Emulator

	Authors:		José Manuel Díez

	Description:	Handles opcode instructions in the
					virtual machine.

**/

#define PRIVATE_VM_ACCESS

#include <stdlib.h>
#include "dcpubase.h"
#include "hw.h"
#include "lem1802.h"

#define HW_MAX 10

int vm_hw_connected[HW_MAX] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
hw_t vm_hw_list[HW_MAX];

uint16_t vm_hw_register(vm_t* vm, hw_t hardware)
{
	uint16_t id = 0;
	while (vm_hw_connected[id] != 0 && id < HW_MAX)
		id++;
	if (id >= HW_MAX)
	{
		vm_halt(vm, "unable to register hardware, maximum reached!");
		return;
	}

	hardware.handler(vm);

	vm_hw_connected[id] = 1;
	vm_hw_list[id] = hardware;
	
	return id;
}

void vm_hw_unregister(vm_t* vm, uint16_t id)
{
	vm_hw_connected[id] = 0;
}

void vm_hw_interrupt(vm_t* vm, uint16_t index)
{
	hw_t device = vm_hw_list[index-1];
	if(vm->debug) printf("\nInterrupting device 0x%04X (0x%04X%04X): %d\n", index, device.id_1, device.id_2, device.handler);
	device.handler(vm);
}

uint16_t vm_hw_count(vm_t* vm)
{
	uint16_t i = 0;
	for(i = 0; i < HW_MAX; i++) {
		if(vm_hw_connected[i] == 0) 
			return i;
	}
}

hw_t vm_hw_get_device(vm_t* vm, uint16_t index) {
	return vm_hw_list[index];
}