#!/usr/bin/env bash
#
# prints out a set of motr parameters for a 
# single connection.
#
# Ganesan Umanesan <ganesan.umanesan@seagate.com>
# 14/12/2020 - initial script
# 28/12/2020 - MIO yaml format added
# 05/01/2021 - Bash export format added 
# 20/02/2021 - hastatus.yaml in /etc/motr
#

hastatusetc='/etc/motr/hastatus.yaml'
hastatustmp=$(mktemp --tmpdir hastatus.XXXXXX)

cleanup()
{
        rm -f $hastatustmp
}

trap cleanup EXIT

#c="client-22"
#ssh client-21 hctl status > $hastatus
c=$HOSTNAME

[[ -f "$hastatusetc" ]] && hastatus=$hastatusetc
[[ ! -f "$hastatusetc" ]] && hctl status > $hastatustmp && hastatus=$hastatustmp

r=$((0 + $RANDOM % 16))
p=()

# HA_ENDPOINT_ADDR
# p[0]=$(grep -A1 client-21 $hastatus | tail -n1 | awk '{print $4}')
p[0]=$(grep -A1 $c $hastatus | tail -n1 | awk '{print $4}')
[[ -z "${p[0]}" ]] && { echo "Error: HA_ENDPOINT_ADDR not found"; exit 1; }

# PROFILE_FID
p[1]=$(grep -A2 Profile $hastatus | tail -n1 | awk '{print $1}')
[[ -z "${p[1]}" ]] && { echo "Error: PROFILE_FID not found"; exit 1; }

# Data pools
p[2]=$(sed -n '/pools:/,/^[A-Za-z]/p' $hastatus | grep -E 'tier.+p1' | awk '{print $1}')
p[3]=$(sed -n '/pools:/,/^[A-Za-z]/p' $hastatus | grep -E 'tier.+p2' | awk '{print $1}')
p[4]=$(sed -n '/pools:/,/^[A-Za-z]/p' $hastatus | grep -E 'tier.+p3' | awk '{print $1}')
[[ -z "${p[2]}" ]] && { echo "Error: M0_POOL_TIER1 not found"; exit 1; }
[[ -z "${p[3]}" ]] && { echo "Error: M0_POOL_TIER2 not found"; exit 1; }
[[ -z "${p[4]}" ]] && { echo "Error: M0_POOL_TIER3 not found"; exit 1; }

# LOCAL_ENDPOINT_ADDR0
p[5]=$(grep -A$((2+($r)%16)) $c $hastatus | tail -n1 | awk '{print $4}')
[[ -z "${p[5]}" ]] && { echo "Error: LOCAL_ENDPOINT_ADDR0 not found"; exit 1; }

# LOCAL_PROC_FID0
p[6]=$(grep -A$((2+($r)%16)) $c $hastatus | tail -n1 | awk '{print $3}')
[[ -z "${p[6]}" ]] && { echo "Error: LOCAL_PROC_FID0 not found"; exit 1; }

usage()
{
    cat <<USAGE_END
Usage: $(basename $0) [-h|--help] [options]
	
    -m|--mio	print mio configuration parameters

    -e|--exp	print in shell export format

    -h|--help	Print this help screen.
USAGE_END

	exit 1
}

exp()
{
	read -r -d '' BASH <<EOF
# $USER $HOSTNAME
# Bash shell export format
export CLIENT_LADDR="${p[5]}"
export CLIENT_HA_ADDR="${p[0]}"
export CLIENT_PROFILE="${p[1]}"
export CLIENT_PROC_FID="${p[6]}"
EOF
	echo "$BASH"
}

mio()
{
	read -r -d '' YAML <<EOF
# $USER $HOSTNAME
# MIO configuration Yaml file. 
# MIO_Config_Sections: [MIO_CONFIG, MOTR_CONFIG]
MIO_CONFIG:
  MIO_LOG_DIR:
  MIO_LOG_LEVEL: MIO_DEBUG 
  MIO_DRIVER: MOTR
  MIO_TELEMETRY_STORE: ADDB
  
MOTR_CONFIG:
  MOTR_USER_GROUP: motr 
  MOTR_INST_ADDR: ${p[5]}
  MOTR_HA_ADDR: ${p[0]}
  MOTR_PROFILE: <${p[1]}>
  MOTR_PROCESS_FID: <${p[6]}>
  MOTR_DEFAULT_UNIT_SIZE: 1048576
  MOTR_IS_OOSTORE: 1
  MOTR_IS_READ_VERIFY: 0
  MOTR_TM_RECV_QUEUE_MIN_LEN: 64
  MOTR_MAX_RPC_MSG_SIZE: 131072
  MOTR_POOLS:
     # Set SAGE cluster pools, ranking from high performance to low. 
     # The pool configuration parameters can be queried using hare.
     # MOTR_POOL_TYPE currently Only supports HDD, SSD or NVM.
     - MOTR_POOL_NAME:	Pool1  
       MOTR_POOL_ID:  	${p[2]}
       MOTR_POOL_TYPE: 	NVM
     - MOTR_POOL_NAME: 	Pool2
       MOTR_POOL_ID:	${p[3]}   
       MOTR_POOL_TYPE:	SSD
     - MOTR_POOL_NAME: 	Pool3
       MOTR_POOL_ID: 	${p[4]}
       MOTR_POOL_TYPE: 	HDD
EOF

	echo "$YAML"
}

#
# MAIN
#

# options
TEMP=$( getopt -o meh --long mio,exp,help -n "$PROG_NAME" -- "$@" )
[[ $? != 0 ]] && usage
eval set -- "$TEMP"

while true ; 
	do
    case "$1" in
     	-m|--mio)
		mio
		exit 0
    	shift
    	;;  	
     	-e|--exp)
		exp
		exit 0
    	shift
    	;;  	
     	-h|--help)
    	usage
    	shift
    	;;  	
		--)     
       	shift
     	break 
     	;;
    	*)
      	echo 'getopt: internal error...'
      	exit 1 
      	;;
   	esac
    done

echo "#"
echo "# USER: $USER"
echo "# Application: All"
echo "#"

echo
echo "HA_ENDPOINT_ADDR = ${p[5]%@*}@${p[0]#*@}"
echo "PROFILE_FID = ${p[1]}"

echo
echo "M0_POOL_TIER1 = ${p[2]}"
echo "M0_POOL_TIER2 = ${p[3]}"
echo "M0_POOL_TIER3 = ${p[4]}"

echo
idx=0; while read line; do
	# match until config for current node over
	if [[ ! "$line" =~ \[.*\] ]]; then
		break
	fi

	# match config for m0_client
	if [[ ! -z "$(echo "${line}" | grep m0_client)" ]]; then
		fid=$(echo "${line}" | awk '{print $3;}')
		addr=$(echo "${line}" | awk '{print $4;}')
		echo "LOCAL_ENDPOINT_ADDR$((idx)) = ${addr}"
		echo "LOCAL_PROC_FID$((idx++)) = ${fid}"
	fi
done < <(grep -A17 $c $hastatus | sed -n '1!p') # match 17 lines but skip first
