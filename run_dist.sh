#!/bin/bash
 
MODEL="$3-rgs.bdd"
RESULT_DIR="result/$3"

sbatch --exclusive --time=180 --nodes=$1 --error=$RESULT_DIR/result-$1-$2.err --output=$RESULT_DIR/result-$1-$2.out submit.sh $1 $2 $(($1*$2)) models/$MODEL
