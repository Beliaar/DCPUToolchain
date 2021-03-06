/**

	File:		bfile.c

	Project:	DCPU-16 Toolchain
	Component:	LibDCPU

	Authors:	James Rhodes

	Description:	Provides buffered file access.

**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bfile.h"
#include "iio.h"

BFILE* bfopen(const char* path, const char* mode)
{
	BFILE* file = (BFILE*)malloc(sizeof(BFILE));
	file->file = fopen(path, mode);
	if (file->file == NULL)
	{
		free(file);
		return NULL;
	}
	file->readable = (mode[0] == 'r' || mode[strlen(mode) - 1] == '+');
	file->last = -1;
	file->eof = false;
	file->wrapped = false;
	if (file->readable)
	{
		file->last = fgetc(file->file);
		file->eof = feof(file->file);
	}
	return file;
}

BFILE* bfwrap(FILE* _file, const char* mode)
{
	BFILE* file = (BFILE*)malloc(sizeof(BFILE));
	file->file = _file;
	if (file->file == NULL)
	{
		free(file);
		return NULL;
	}
	file->readable = (mode[0] == 'r' || mode[strlen(mode) - 1] == '+');
	file->last = -1;
	file->eof = false;
	file->wrapped = true;
	if (file->readable)
	{
		file->last = fgetc(file->file);
		file->eof = feof(file->file);
	}
	return file;
}

int bfgetc(BFILE* file)
{
	int c;
	assert(file != NULL && file->readable);
	c = file->last;
	file->last = fgetc(file->file);
	file->eof = feof(file->file);
	return c;
}

int bfputc(int chr, BFILE* file)
{
	assert(file != NULL);
	return fputc(chr, file->file);
}

long bftell(BFILE* file)
{
	assert(file != NULL);
	// TODO: Is this correct?
	if (file->eof)
		return ftell(file->file);
	else
		return ftell(file->file) - 1;
}

int bfseek(BFILE* file, long offset, int origin)
{
	assert(file != NULL);
	return fseek(file->file, offset, origin);
}

int bfeof(BFILE* file)
{
	assert(file != NULL);
	return file->eof;
}

void bfclose(BFILE* file)
{
	assert(file != NULL);
	if (!file->wrapped)
		fclose(file->file);
	file->file = NULL;
	file->eof = true;
	file->last = -1;
	file->readable = false;
	free(file);
}

size_t bfread(void* dest, size_t elem, size_t count, BFILE* file)
{
	size_t result;
	assert(file != NULL && file->readable);
	fseek(file->file, -1, SEEK_CUR);
	result = fread(dest, elem, count, file->file);
	file->last = fgetc(file->file);
	file->eof = feof(file->file);
	return result;
}

size_t bfwrite(const void* src, size_t elem, size_t count, BFILE* file)
{
	return fwrite(src, elem, count, file->file);
}

size_t bfiread(uint16_t* dest, BFILE* file)
{
	size_t result;
	assert(file != NULL && file->readable);
	fseek(file->file, -1, SEEK_CUR);
	result = iread(dest, file->file);
	file->last = fgetc(file->file);
	file->eof = feof(file->file);
	return result;
}

size_t bfiwrite(const uint16_t* src, BFILE* file)
{
	return iwrite(src, file->file);
}