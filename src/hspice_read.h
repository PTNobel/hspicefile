#include "Python.h"

// Structure for fast vector access
struct FastArray
{
	char *data;
	char *pos;
	Py_ssize_t stride;
	Py_ssize_t length;
};

// Python callable function
static PyObject *HSpiceRead(PyObject *self, PyObject *args);

#ifdef LINUX
#define __declspec(a) extern
#endif
