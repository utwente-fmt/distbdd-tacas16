#!/bin/bash
 
upcrun -n $3 -N $1 -bind-threads -shared-heap 8GB -fca_enable 0 distbdd $4
