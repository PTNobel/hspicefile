// (c) Parth Nobel
// Date: 18.10.2018
// Modifications:
// 1. Replaced int with size_t in a number of places to allow .tr0 files
//    of size > 1GiB to be read.

// (c) Janez Puhan
// Date: 18.5.2009
// HSpice binary file import module
// Modifications:
// 1. All vector names are converted to lowercase.
// 2. In vector names 'v(*' is converted to '*'. 
// 3. No longer try to close a file after failed fopen (caused a crash). 
// Author: Arpad Buermen

// Note that in Windows we do not use Debug compile because we don't have the debug
// version of Python libraries and interpreter. We use Release version instead where
// optimizations are disabled. Such a Release version can be debugged.

#include "Python.h"
#include "arrayobject.h"
#include "hspice_read.h"



struct module_state {
    PyObject *error;
};
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static PyObject *
error_out(PyObject *m) {
    struct module_state *st = GETSTATE(m);
    PyErr_SetString(st->error, "something bad happened");
    return NULL;
}
static PyMethodDef _hspice_read_methods[] =
{
	{"hspice_read", HSpiceRead, METH_VARARGS},
        {"error_out", (PyCFunction)error_out, METH_NOARGS, NULL},
	{NULL, NULL}	// Marks the end of this structure.
};

static int _hspice_read_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}
static int _hspice_read_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "hspiceread",
  NULL,
  sizeof(struct module_state),
  _hspice_read_methods,
  NULL,
  _hspice_read_traverse,
  _hspice_read_clear,
  NULL
};

// Module initialization
// Module name must be _hspice_read in compile and link.
__declspec(dllexport) PyObject *PyInit__hspice_read()
{
        PyObject *module = PyModule_Create(&moduledef);
	if (module == NULL) return NULL;

	struct module_state *st = GETSTATE(module);

	st->error = PyErr_NewException("_hspice_read.Error", NULL, NULL);
	if (st->error == NULL) {
            Py_DECREF(module);
	    return NULL;
	}

	import_array();  // Must be present for NumPy.
	return module;
}


#define debugFile						stdout

// Header character positions
#define blockHeaderSize					4
#define numOfVariablesPosition			0
#define numOfProbesPosition				4
#define numOfSweepsPosition				8
#define numOfSweepsEndPosition			12
#define postStartPosition1				16
#define postStartPosition2				20
#define postString11					"9007"
#define postString12					"9601"
#define postString21					"2001"
#define numOfPostCharacters				4
#define dateStartPosition				88
#define dateEndPosition					112
#define titleStartPosition				24
#define sweepSizePosition1				176
#define sweepSizePosition2				187
#define vectorDescriptionStartPosition	256
#define frequency						2
#define complex_var						1
#define real_var						0

// Perform endian swap on array of numbers. Arguments:
//   block    ... pointer to array of numbers
//   size     ... size of the array
//   itemSize ... size of one number in the array in bytes
void do_swap(char *block, int size, int itemSize)
{
	int i;
	for(i = 0; i < size; i++)
	{
		int j;
		for(j = 0; j < itemSize / 2; j++)
		{
			char tmp = block[j];
			block[j] = block[itemSize - j - 1];
			block[itemSize - j - 1] = tmp;
		}
		block = block + itemSize;
	}
}

// Read block header. Returns:
//   -1 ... block header corrupted
//    0 ... endian swap not performed
//    1 ... endian swap performed
// Arguments:
//   f           ... pointer to file for reading
//   fileName    ... name of the file
//   debugMode   ... debug messages flag
//   blockHeader ... array of four integers consisting block header
//   size        ... size of items in block
int readBlockHeader(FILE *f, const char *fileName, int debugMode, int *blockHeader,
					int size)
{
	int swap, num = fread(blockHeader, sizeof(int), blockHeaderSize, f);
	if(num != blockHeaderSize)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to read block header from file %s.\n",
							  fileName);
		return -1;	// Error.
	}

	// Block header check and swap.
	if(blockHeader[0] == 0x00000004 && blockHeader[2] == 0x00000004) swap = 0;
	else if(blockHeader[0] == 0x04000000 && blockHeader[2] == 0x04000000) swap = 1;
	else
	{
		if(debugMode) fprintf(debugFile, "HSpiceRead: corrupted block header.\n");
		return -1;
	}
	if(swap == 1) do_swap((char *)blockHeader, blockHeaderSize, sizeof(int));
	blockHeader[0] = blockHeader[blockHeaderSize - 1] / size;
	return swap;
}

// Read block data. Returns:
//   0 ... reading performed normally
//   1 ... reading failed
// Arguments:
//   f           ... pointer to file for reading
//   fileName    ... name of the file
//   debugMode   ... debug messages flag
//   ptr         ... pointer to reserved space for data
//   offset      ... pointer to reserved space size,
//                   increased for current block size
//   itemSize    ... size of one item in block
//   numOfItems  ... number of items in block
//   swap        ... perform endian swap flag
int readBlockData(FILE *f, const char *fileName, int debugMode, void *ptr,
					size_t *offset, int itemSize, int numOfItems, int swap)
{
	int num = fread(ptr, itemSize, numOfItems, f);
	if(num != numOfItems)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to read block from file %s.\n",
							  fileName);
		return 1;	// Error.
	}
	*offset = *offset + numOfItems;
	if(swap > 0) do_swap((char *)ptr, numOfItems, itemSize);	// Endian swap.
	return 0;
}

// Read block trailer. Returns:
//   0 ... reading performed normally
//   1 ... block trailer corrupted
// Arguments:
//   f           ... pointer to file for reading
//   fileName    ... name of the file
//   debugMode   ... debug messages flag
//   swap        ... perform endian swap flag
//   header      ... block size from header
int readBlockTrailer(FILE *f, const char *fileName, int debugMode, int swap,
					 int header)
{
	int trailer, num;
	num = fread(&trailer, sizeof(int), 1, f);
	if(num != 1)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to read block trailer from file %s.\n",
							  fileName);
		return 1;	// Error.
	}
	if(swap > 0) do_swap((char *)(&trailer), 1, sizeof(int));	// Endian swap.

	// Block header and trailer match check.
	if(header != trailer)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: block header and trailer mismatch.\n");
		return 1;	// Error.
	}

	return 0;
}

// Reallocate space. Returns:
// 	 NULL    ... reallocation failed
//   pointer ... address of reallocated space
// Arguments:
//   debugMode   ... debug messages flag
//   ptr         ... pointer to already allocated space
//   size        ... new size in bytes
void *reallocate(int debugMode, void *ptr, size_t size)
{
	// Allocate space for raw data.
	void *tmp = PyMem_Realloc(ptr, size);
	if(tmp == NULL && debugMode)
		fprintf(debugFile, "HSpiceRead: cannot allocate.\n");
	return tmp;
}

// Read one file header block. Returns:
//   -1 ... this was the last block
//    0 ... there is at least one more block left
//    1 ... error occured during reading the block
// Arguments:
//   f         ... pointer to file for reading
//   debugMode ... debug messages flag
//   fileName  ... name of the file
//   buf       ... pointer to header buffer,
//                 enlarged (reallocated) for current block
//   bufOffset ... pointer to buffer size, increased for current block size
int readHeaderBlock(FILE *f, int debugMode, const char *fileName, char **buf,
					size_t *bufOffset)
{
	char *tmpBuf;
	int error, blockHeader[blockHeaderSize], swap;

	// Get size of file header block.
	swap = readBlockHeader(f, fileName, debugMode, blockHeader, sizeof(char));
	if(swap < 0) return 1;	// Error.

	// Allocate space for buffer.
	tmpBuf = reallocate(debugMode, *buf,
						(*bufOffset + blockHeader[0] + 1) * sizeof(char));
	if(tmpBuf == NULL) return 1;	// Error.
	*buf = tmpBuf;

	// Read file header block.
	error = readBlockData(f, fileName, debugMode, *buf + *bufOffset, bufOffset,
						  sizeof(char), blockHeader[0], 0);
	if(error == 1) return 1;	// Error.
	(*buf)[*bufOffset] = 0;

	// Read trailer of file header block.
	error = readBlockTrailer(f, fileName, debugMode, swap,
							 blockHeader[blockHeaderSize - 1]);
	if(error == 1) return 1;	// Error.

	if(strstr(*buf, "$&%#")) return -1;	// End of block.

	return 0;	// There is more.
}

// Get sweep infornation from file header block. Returns:
//   0 ... performed normally
//   1 ... error occurred
// Arguments:
//   debugMode   ... debug messages flag
//   sweep       ... acquired sweep parameter name, new reference created
//   buf         ... header string
//   sweepSize   ... acquired number of sweep points
//   sweepValues ... sweep points array, new reference created
//   faSweep     ... pointer to fast access structure for sweep array
int getSweepInfo(int debugMode, PyObject **sweep, char *buf, int *sweepSize,
				 PyObject **sweepValues, struct FastArray *faSweep)
{
	char *sweepName = NULL;
	npy_intp dims;
	sweepName = strtok(NULL, " \t\n");	// Get sweep parameter name.
	if(sweepName == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to extract sweep name.\n");
		return 1;
	}
	*sweep = PyUnicode_FromString(sweepName);
	if(*sweep == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create sweep name string.\n");
		return 1;
	}

	// Get number of sweep points.
	if(strncmp(&buf[postStartPosition2], postString21, numOfPostCharacters) != 0)
		*sweepSize = atoi(&buf[sweepSizePosition1]);
	else *sweepSize = atoi(&buf[sweepSizePosition2]);

	// Create array for sweep parameter values.
	dims=*sweepSize;
	*sweepValues = PyArray_SimpleNew(1, &dims, PyArray_DOUBLE);
	if(*sweepValues == NULL)
	{
		if(debugMode) fprintf(debugFile, "HSpiceRead: failed to create array.\n");
		return 1;
	}

	// Prepare fast access structure.
	faSweep->data = ((PyArrayObject *)(*sweepValues))->data;
	faSweep->pos = ((PyArrayObject *)(*sweepValues))->data;
	faSweep->stride =
		((PyArrayObject *)(*sweepValues))->strides[((PyArrayObject *)(*sweepValues))->nd -
		1];
	faSweep->length = PyArray_Size(*sweepValues);

	return 0;
}

// Read one data block. Returns:
//   -1 ... this was the last block
//    0 ... there is at least one more block left
//    1 ... error occured during reading the block
// Arguments:
//   f             ... pointer to file for reading
//   debugMode     ... debug messages flag
//   fileName      ... name of the file
//   rawData       ... pointer to data array,
//                     enlarged (reallocated) for current block
//   rawDataOffset ... pointer to data array size, increased for current block size
int readDataBlock(FILE *f, int debugMode, const char *fileName, float **rawData,
				  size_t *rawDataOffset)
{
	int error, blockHeader[blockHeaderSize], swap;
	float *tmpRawData;

	// Get size of raw data block.
	swap = readBlockHeader(f, fileName, debugMode, blockHeader, sizeof(float));
	if(swap < 0) return 1;	// Error.

	// Allocate space for raw data.
	tmpRawData = reallocate(debugMode, *rawData,
							(*rawDataOffset + blockHeader[0]) * sizeof(float));
	if(tmpRawData == NULL) return 1;	// Error.
	*rawData = tmpRawData;

	// Read raw data block.
	error = readBlockData(f, fileName, debugMode, *rawData + *rawDataOffset,
						  rawDataOffset, sizeof(float), blockHeader[0], swap);
	if(error == 1) return 1;	// Error.

	// Read trailer of file header block.
	error = readBlockTrailer(f, fileName, debugMode, swap,
							 blockHeader[blockHeaderSize - 1]);
	if(error == 1) return 1;	// Error.

	if((*rawData)[*rawDataOffset - 1] > 9e29) return -1;	// End of block.

	return 0;	// There is more.
}

// Read one table for one sweep value. Returns:
//   0 ... performed normally
//   1 ... error occurred
// Arguments:
//   f              ... pointer to file for reading
//   debugMode      ... debug messages flag
//   fileName       ... name of the file
//   sweep          ... sweep parameter name
//   numOfVariables ... number of variables in table
//   type           ... type of variables with exeption of scale 
//   numOfVectors   ... number of variables and probes in table
//   faSweep        ... pointer to fast access structure for sweep array
//   tmpArray       ... array of pointers to arrays
//   faPtr          ... array of fast access structures for vector arrays
//   scale          ... scale name
//   name           ... array of vector names
//   dataList       ... list of data dictionaries
int readTable(FILE *f, int debugMode, const char *fileName, PyObject *sweep,
			  int numOfVariables, int type, int numOfVectors,
			  struct FastArray *faSweep, PyObject **tmpArray,
			  struct FastArray *faPtr, char *scale, char **name, PyObject *dataList)
{
	int i, j, num, numOfColumns = numOfVectors;
    size_t offset = 0;
	npy_intp dims;
	float *rawDataPos, *rawData = NULL;
	PyObject *data = NULL; 

	// Read raw data blocks.
	do num = readDataBlock(f, debugMode, fileName, &rawData, &offset);
	while(num == 0);
	if(num > 0) goto readTableFailed;

	data = PyDict_New();	// Create an empty dictionary.
	if(data == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create data dictionary.\n");
		goto readTableFailed;
	}

	// Increase number of columns if variables with exeption of scale are complex.
	if(type == complex_var) numOfColumns = numOfColumns + numOfVariables - 1;

	rawDataPos = rawData;
	if(sweep == NULL) num = (offset - 1) / numOfColumns;	// Number of rows.
	else
	{
		num = (offset - 2) / numOfColumns;
		*((npy_double *)(faSweep->pos)) = *rawDataPos;	// Save sweep value.
		rawDataPos = rawDataPos + 1;
		faSweep->pos = faSweep->pos + faSweep->stride;
	}

	for(i = 0; i < numOfVectors; i++)
	{
		// Create array for i-th vector.
		dims=num;
		if(type == complex_var && i > 0 && i < numOfVariables)
			tmpArray[i] = PyArray_SimpleNew(1, &dims, PyArray_CDOUBLE);
		else
			tmpArray[i] = PyArray_SimpleNew(1, &dims, PyArray_DOUBLE);
		if(tmpArray[i] == NULL)
		{
			if(debugMode)
				fprintf(debugFile, "HSpiceRead: failed to create array.\n");
			for(j = 0; j < i + 1; j++) Py_XDECREF(tmpArray[j]);
			goto readTableFailed;
		}
	}

	for(i = 0; i < numOfVectors; i++)	// Prepare fast access structures.
	{
		faPtr[i].data = ((PyArrayObject *)(tmpArray[i]))->data;
		faPtr[i].pos = ((PyArrayObject *)(tmpArray[i]))->data;
		faPtr[i].stride =
			((PyArrayObject *)(tmpArray[i]))->strides[((PyArrayObject *)(tmpArray[i]))->nd -
			1];
		faPtr[i].length = PyArray_Size(tmpArray[i]);
	}

	for(i = 0; i < num; i++)	// Save raw data.
	{
		struct FastArray *faPos = faPtr;
		for(j = 0; j < numOfVectors; j++)
		{
			if(type == complex_var && j > 0 && j < numOfVariables)
			{
				((npy_cdouble *)(faPos->pos))->real = *rawDataPos;
				rawDataPos = rawDataPos + 1;
				((npy_cdouble *)(faPos->pos))->imag = *rawDataPos;
			} else *((npy_double *)(faPos->pos)) = *rawDataPos;
			rawDataPos = rawDataPos + 1;
			faPos->pos = faPos->pos + faPos->stride;
			faPos = faPos + 1;
		}
	}
	PyMem_Free(rawData);
	rawData = NULL;

	// Insert vectors into dictionary.
	num = PyDict_SetItemString(data, scale, tmpArray[0]);
	i = -1;
	if(num == 0) for(i = 0; i < numOfVectors - 1; i++)
	{
		num = PyDict_SetItemString(data, name[i], tmpArray[i + 1]);
		if(num != 0) break;
	}
	for(j = 0; j < numOfVectors; j++) Py_XDECREF(tmpArray[j]);
	if(num)
	{
	  if(debugMode) {
	    if(i == -1)
	      fprintf(debugFile, "HSpiceRead: failed to insert vector %s into dictionary.\n", scale);
	    else
	      fprintf(debugFile, "HSpiceRead: failed to insert vector %s into dictionary.\n", name[i]);
	  }
	  goto readTableFailed;
	}

	// Insert table into the list of data dictionaries.
	num = PyList_Append(dataList, data);
	if(num)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to append table to the list of data dictionaries.\n");
		goto readTableFailed;
	}
	Py_XDECREF(data);
	data = NULL;

	return 0;

readTableFailed:
	PyMem_Free(rawData);
	Py_XDECREF(data);
	return 1;
}

// This is the first prototype version of HSpiceRead function for reading HSpice
// output files.
// TODO:
//   ascii format support
//   different vector types support (like voltage, current ..., although I do not
//                                   know what it would be good for)
//   scale monotonity check
static PyObject *HSpiceRead(PyObject *self, PyObject *args)
{
	const char *fileName;
	char *token, *buf = NULL, **name = NULL;
	int debugMode, num, numOfVectors, numOfVariables, type, sweepSize = 1,
		i = dateStartPosition - 1;
    size_t offset = 0;
	struct FastArray faSweep, *faPtr = NULL;
	FILE *f = NULL;
	PyObject *date = NULL, *title = NULL, *scale = NULL, *sweep = NULL,
		*sweepValues = NULL, *dataList = NULL, **tmpArray = NULL, *sweeps = NULL,
		*tuple = NULL, *list = NULL;

	// Get hspice_read() arguments.
	if(!PyArg_ParseTuple(args, "si", &fileName, &debugMode)) return Py_None;

	if(debugMode) fprintf(debugFile, "HSpiceRead: reading file %s.\n", fileName);

	f = fopen(fileName, "rb");	// Open the file.
	if(f == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: cannot open file %s.\n", fileName);
		goto failed;
	}

	num = getc(f);
	ungetc(num, f);
	if(num == EOF)	// Test if there is data in the file.
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: file %s is empty.\n", fileName);
		goto failed;
	}
	if((num & 0x000000ff) >= ' ')	// Test if the file is in binary format.
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: file %s is in ascii format.\n",
							  fileName);
		goto failed;
	}

	// Read file header blocks.
	do num = readHeaderBlock(f, debugMode, fileName, &buf, &offset);
	while(num == 0);
	if(num > 0) goto failed;

 	// Check version of post format.
	if(strncmp(&buf[postStartPosition1], postString11, numOfPostCharacters) != 0 &&
	   strncmp(&buf[postStartPosition1], postString12, numOfPostCharacters) != 0 &&
	   strncmp(&buf[postStartPosition2], postString21, numOfPostCharacters) != 0)
	{
		if(debugMode) fprintf(debugFile, "HSpiceRead: unknown post format.\n");
		goto failed;
	}

	buf[dateEndPosition] = 0;
	date = PyUnicode_FromString(&buf[dateStartPosition]);	// Get creation date.
	if(date == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create date string.\n");
		goto failed;
	}

	while(buf[i] == ' ') i--;
	buf[i + 1] = 0;
	title = PyUnicode_FromString(&buf[titleStartPosition]);	// Get title.
	if(title == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create title string.\n");
		goto failed;
	}

	buf[numOfSweepsEndPosition] = 0;	// Check number of sweep parameters.
	num = atoi(&buf[numOfSweepsPosition]);
	if(num < 0 || num > 1) 
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: only onedimensional sweep supported.\n");
		goto failed;
	}

	buf[numOfSweepsPosition] = 0;	// Get number of vectors (variables and probes).
	numOfVectors = atoi(&buf[numOfProbesPosition]);
	buf[numOfProbesPosition] = 0;
	numOfVariables = atoi(&buf[numOfVariablesPosition]);	// Scale included.
	numOfVectors = numOfVectors + numOfVariables;

	// Get type of variables. Scale is always real.
	token = strtok(&buf[vectorDescriptionStartPosition], " \t\n");
	type = atoi(token);
	if(type == frequency) type = complex_var;
	else type = real_var;

	for(i = 0; i < numOfVectors; i++) token = strtok(NULL, " \t\n");
	if(token == NULL)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to extract independent variable name.\n");
		goto failed;
	}

	scale = PyUnicode_FromString(token);	// Get independent variable name.
	if(scale == NULL)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to create independent variable name string.\n");
		goto failed;
	}

	// Allocate space for pointers to vector names.
	name = (char **)PyMem_Malloc((numOfVectors - 1) * sizeof(char *));
	if(name == NULL)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: cannot allocate pointers to vector names.\n");
		goto failed;
	}

	for(i = 0; i < numOfVectors - 1; i++)	// Get vector names.
	{
		name[i] = strtok(NULL, " \t\n");
		if(name[i] == NULL)
		{
			if(debugMode) fprintf(debugFile,
								  "HSpiceRead: failed to extract vector names.\n");
			goto failed;
		}
	}

	// Process vector names: make name lowercase, remove v( in front of name
	for(i=0; i < numOfVectors - 1; i++) {
		int j;
		for(j=0;name[i][j];j++) {
			if (name[i][j]>='A' && name[i][j]<='Z') {
				name[i][j]-='A'-'a';
			}
		}
		if (name[i][0]=='v' && name[i][1]=='(') {
			for(j=2;name[i][j];j++) {
				name[i][j-2]=name[i][j];
			}
			name[i][j-2]=0;
		}
	}

	if(num == 1)	// Get sweep information.
	{
		int num = getSweepInfo(debugMode, &sweep, buf, &sweepSize, &sweepValues,
							   &faSweep);
		if(num) goto failed;
	}

	dataList = PyList_New(0);	// Create an empty list for data dictionaries.
	if(dataList == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create data list.\n");
		goto failed;
	}

	// Allocate space for pointers to arrays.
	tmpArray = (PyObject **)PyMem_Malloc(numOfVectors * sizeof(PyObject *));
	if(tmpArray == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: cannot allocate pointers to arrays.\n");
		goto failed;
	}

	// Allocate space for fast array pointers.
	faPtr =
		(struct FastArray *)PyMem_Malloc(numOfVectors * sizeof(struct FastArray));
	if(faPtr == NULL)
	{
		if(debugMode) fprintf(debugFile, "HSpiceRead: failed to create array.\n");
		goto failed;
	}

	for(i = 0; i < sweepSize; i++)	// Read i-th table.
	{
		num = readTable(f, debugMode, fileName, sweep, numOfVariables, type,
						numOfVectors, &faSweep, tmpArray, faPtr, token, name,
						dataList);
		if(num) goto failed;
	}
	fclose(f);
	f = NULL;
	PyMem_Free(faPtr);
	faPtr = NULL;
	PyMem_Free(buf);
	buf = NULL;
	PyMem_Free(name);
	name = NULL;
	PyMem_Free(tmpArray);
	tmpArray = NULL;

	// Create sweeps tuple.
	if(sweep == NULL) sweeps = PyTuple_Pack(3, Py_None, Py_None, dataList);
	else sweeps = PyTuple_Pack(3, sweep, sweepValues, dataList);
	if(sweeps == NULL)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to create tuple with sweeps.\n");
		goto failed;
	}
	Py_XDECREF(sweep);
	Py_XDECREF(sweepValues);
	Py_XDECREF(dataList);

	// Prepare return tuple.
	tuple = PyTuple_Pack(6, sweeps, scale, Py_None, title, date, Py_None);
	if(tuple == NULL)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to create tuple with read data.\n");
		goto failed;
	}
	Py_XDECREF(date);
	date = NULL;
	Py_XDECREF(title);
	title = NULL;
	Py_XDECREF(scale);
	scale = NULL;
	Py_XDECREF(sweeps);
	sweeps = NULL;

	list = PyList_New(0);	// Create an empty list.
	if(list == NULL)
	{
		if(debugMode)
			fprintf(debugFile, "HSpiceRead: failed to create return list.\n");
		goto failed;
	}

	num = PyList_Append(list, tuple);	// Insert tuple into return list.
	if(num)
	{
		if(debugMode) fprintf(debugFile,
							  "HSpiceRead: failed to append tuple to return list.\n");
		goto failed;
	}
	Py_XDECREF(tuple);

	return list;

failed:	// Error occured. Close open file, relese memory and python references.
	if (f)
		fclose(f);
	PyMem_Free(buf);
	Py_XDECREF(date);
	Py_XDECREF(title);
	Py_XDECREF(scale);
	PyMem_Free(name);
	Py_XDECREF(sweep);
	Py_XDECREF(sweepValues);
	Py_XDECREF(dataList);
	PyMem_Free(tmpArray);
	PyMem_Free(faPtr);
	Py_XDECREF(sweeps);
	Py_XDECREF(tuple);
	Py_XDECREF(list);
	return Py_None;
}
