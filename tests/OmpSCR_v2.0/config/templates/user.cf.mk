#
# OpenMP Source Code Repository
#
# COMPILER CONFIGURATION MAKEFILE
#
# This file include details about your C and/or Fortran90/95 compilers 
# and compilation flags, common to all applications
#


########################################
#
# SECTION 1: C COMPILER
#

#
# 1.1. C compiler activation
#	A value of "y" will enable C source code compilation
#	A value of "n" will unable C source code compilation
#
OSCR_USE_C=y

#
# 1.2. The name of your C compiler or front-end
#
OSCR_CC=gcc

#
# 1.3. Flag/s needed to activate OpenMP pragmas recognition
#
OSCR_C_OMPFLAG=-fopenmp -lomp

#
# 1.4. Flag/s needed for serial compilation (No OpenMP)
#
OSCR_C_OMPSTUBSFLAG=

#
# 1.5. (Optional)
#	Flags to obtain some report or information about the parallelization
#
OSCR_C_REPORT=

#
# 1.6. (Optional) Other common flags (e.g. optimization)
#
OSCR_C_OTHERS=-O3



########################################
#
# SECTION 2: C++ COMPILER
#

#
# 2.1. C++ compiler activation
#	A value of "y" will enable C++ source code compilation
#	A value of "n" will unable C++ source code compilation
#
OSCR_USE_CPP=y

#
# 2.2. The name of your C++ compiler or front-end
#
OSCR_CPPC=g++

#
# 2.3. Flag/s needed to activate OpenMP pragmas recognition
#
OSCR_CPP_OMPFLAG=-fopenmp -lomp

#
# 2.4. Flag/s needed for serial compilation (No OpenMP)
#
OSCR_CPP_OMPSTUBSFLAG=

#
# 2.5. (Optional)
#	Flags to obtain some report or information about the parallelization
#
OSCR_CPP_REPORT=

#
# 2.6. (Optional) Other common flags (e.g. optimization)
#
OSCR_CPP_OTHERS=-O3



########################################
#
# SECTION 3: Fortran90/95 COMPILER
#

#
# 3.1. Frotran90/95 compiler activation
#	A value of "y" will enable Fortran90/95 source code compilation
#	A value of "n" will unable Fortran90/95 source code compilation
#
OSCR_USE_F=n

#
# 3.2. The name of your Frotran90/95 compiler or front-end
#
OSCR_FF=

#
# 3.3. Flag/s needed to activate OpenMP pragmas recognition
#
OSCR_F_OMPFLAG=

#
# 3.4. Flag/s needed for serial compilation (No OpenMP)
#
OSCR_F_OMPSTUBSFLAG=

#
# 3.5. (Optional)
#	Flags to obtain some report or information about the parallelization
#
OSCR_F_REPORT=

#
# 3.6. (Optional) Other common flags (e.g. optimization)
#
OSCR_F_OTHERS=



#
# END OF COMPILER CONFIGURATION MAKEFILE
#
########################################
