"""
**HSPICE result file input**

Contributed to the PyOPUS project by Janez Puhan.

Pulled out of PyOPUS to a separate module and ported
to Python3 by Trey Greer
"""

import _hspice_read
from numpy import array
from time import strftime

__all__ = [ 'hspice_read' ]

def hspice_read(filename, debug=0):
	"""
	Returns a list with only one tuple as member (representing the results of 
	one analysis). 
	
	The tuple has the following members
	
	0. Simulation results tuple with following members
	  
	  If a variable was swept and the analysis repeated for every value in the 
	  sweep
	    
		0. The name of the swept parameter
		1. An array with the N values of the parameter
		2. A list with N dictionaries, one for every parameter value holding 
		   the simulation results where result name is the key and values are 
		   arrays. 
	  
	  If no variable was swept and the analysis was performed only once
	    
		0. ``None``
		1. ``None``
		2. A list with one dictionay as the only memebr. The dictionary holds 
		   the simulation results. The name of a result is the key while values 
		   are arrays. 
		   
	1. The name of the default scale array 
	2. ``None`` (would be the dictionary of non-default scale vector names)
	3. Title string
	4. Date string
	5. ``None`` (would be the plot name string)
	
	Returns ``None`` if an error occurs during reading. 
	"""
	return _hspice_read.hspice_read(filename, debug)
