#!/usr/bin/env python

from distutils.core import setup, Extension
from distutils.sysconfig import get_python_inc, PREFIX
import numpy
import os

# Detect platform, set up include directories and preprocessor macros 
define_macros=[('LINUX', None)]
include_dirs=[os.path.join(numpy.get_include(), 'numpy')]
	
# Extensions
ext_modules=[
	Extension(
		'_hspice_read', 
		['src/hspice_read.c'], 
		include_dirs=include_dirs,
		define_macros=define_macros
	) 
]

# Settings
setup(name='hspicefile',
	version='1.01',
	description='read hspice binary files',
    long_description="""
hspicefile provides a simple function to read hspice binary output files.
        
Taken from the PyOPUS package http://fides.fe.uni-lj.si/pyopus and ported
to Python3 by Trey Greer
""",
	author='Janez Puhan',
    maintainer='Trey Greer',
    maintainer_email='tgreer@nvidia.com',
	platforms='Linux', 
    classifiers=[
     'License :: OSI Approved :: GNU General Public License v3 (GPLv3)',
     'Operating System :: POSIX :: Linux',
     'Programming Language :: Python :: 3'],
    py_modules=['hspicefile'],
	ext_modules=ext_modules
)
