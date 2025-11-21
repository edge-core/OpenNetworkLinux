#!/bin/bash

#Tech Support script version
TS_VERSION="0.0.1"

# TRUE=0, FALSE=1
TRUE=0
FALSE=1

# DATESTR: The format of log folder and log file
DATESTR=$(date +"%Y%m%d%H%M%S")
DEFAULT_LOG_FOLDER_NAME="log_platform_${DATESTR}"
DEFAULT_LOG_FILE_NAME="log_platform_${DATESTR}.log"
LOG_FOLDER_NAME=""
LOG_FILE_NAME=""

# LOG_FOLDER_ROOT: The root folder of log files
DEFAULT_LOG_FOLDER_ROOT="/tmp/log"
DEFAULT_LOG_FOLDER_PATH="${DEFAULT_LOG_FOLDER_ROOT}/${DEFAULT_LOG_FOLDER_NAME}"
DEFAULT_LOG_FILE_PATH="${DEFAULT_LOG_FOLDER_PATH}/${DEFAULT_LOG_FILE_NAME}"
LOG_FOLDER_ROOT=""
LOG_FOLDER_PATH=""
LOG_FILE_PATH=""
LOG_FAST=${FALSE}

# HW_REV: set by function _board_info
HW_REV=""

SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# LOG_FILE_ENABLE=1: Log all the platform info to log files (${LOG_FILE_NAME})
# LOG_FILE_ENABLE=0: Print all the platform info in console
LOG_FILE_ENABLE=1

# Log Redirection
# LOG_REDIRECT="2> /dev/null"        : remove the error message from console
# LOG_REDIRECT=""                    : show the error message in console
# LOG_REDIRECT="2>> $LOG_FILE_PATH"  : show the error message in stdout, then stdout may send to console or file in _echo()
LOG_REDIRECT="2>> $LOG_FILE_PATH"

# GPIO_MAX: update by function _update_gpio_max
GPIO_MAX=0
GPIO_MAX_INIT_FLAG=0

# CPLD max index
MAX_CPLD=3

# Execution Time
start_time=$(date +%s)
end_time=0
elapsed_time=0

OPT_BYPASS_I2C_COMMAND=${FALSE}

# NETIF name, should be changed according NOS naming
NET_IF=""

function _echo {
    str="$@"

    if [ "${LOG_FILE_ENABLE}" == "1" ] && [ -f "${LOG_FILE_PATH}" ]; then
        echo "${str}" >> "${LOG_FILE_PATH}"
    else
        echo "${str}"
    fi
}

function _banner {
   banner="$1"

   if [ ! -z "${banner}" ]; then
       _echo ""
       _echo "##############################"
       _echo "#   ${banner}"
       echo  "#   ${banner}..."
       _echo "##############################"
   fi
}

function _pkg_version {
    _banner "Package Version = ${TS_VERSION}"
}

function _show_ts_version {
    echo "Package Version = ${TS_VERSION}"
}

function _check_env {
    echo "#   Check Environment"

    # check basic commands
    cmd_array=("ipmitool" "lsusb" "dmidecode")
    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        ret=`which ${cmd_array[$i]}`

        if [ ! $? -eq 0 ]; then
            _echo "${cmd_array[$i]} command not found!!"
            exit 1
        fi
    done

    if [ "${LOG_FILE_ENABLE}" == "1" ]; then
        mkdir -p "${LOG_FOLDER_PATH}"
        if [ ! -d "${LOG_FOLDER_PATH}" ]; then
            _echo "[ERROR] invalid log path: ${LOG_FOLDER_PATH}"
            exit 1
        fi        
        echo "${LOG_FILE_NAME}" > "${LOG_FILE_PATH}"
    fi 
}

function _check_filepath {
    filepath=$1
    if [ -z "${filepath}" ]; then
        _echo "ERROR, the ipnut string is empyt!!!"
        return ${FALSE}
    elif [ ! -f "$filepath" ]; then
        _echo "ERROR: No such file: ${filepath}"
        return ${FALSE}
    else
        #_echo "File Path: ${filepath}"
        return ${TRUE}
    fi
}

function _check_dirpath {
    dirpath=$1
    if [ -z "${dirpath}" ]; then
        _echo "ERROR, the ipnut string is empty!!!"
        return ${FALSE}
    elif [ ! -d "$dirpath" ]; then
        _echo "ERROR: No such directory: ${dirpath}"
        return ${FALSE}
    else        
        return ${TRUE}
    fi
}

function _check_i2c_device {
    i2c_addr=$1

    if [ -z "${i2c_addr}" ]; then
        _echo "ERROR, the ipnut string is empyt!!!"
        return ${FALSE}
    fi

    value=$(eval "i2cget -y -f 0 ${i2c_addr} ${LOG_REDIRECT}")
    ret=$?

    if [ $ret -eq 0 ]; then
        return ${TRUE}
    else
        _echo "ERROR: No such device: ${i2c_addr}"
        return ${FALSE}
    fi
}

function _check_bmc_device {
    if [[ -e "/dev/ipmi0" ]] || [[ -e "/dev/ipmidev/0" ]];then
        return ${TRUE}
    else
        return ${FALSE}
    fi

}

function _show_system_info {
    _banner "Show System Info"

    x86_date=`date`
    x86_uptime=`uptime`

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${TRUE} ]]; then
        bmc_date=$(eval "ipmitool sel time get ${LOG_REDIRECT}")
    fi
    last_login=`last`

    _echo "[X86 Date Time ]: ${x86_date}"
    if [[ find_bmc_dev -eq ${TRUE} ]]; then
        _echo "[BMC Date Time ]: ${bmc_date}"
    fi
    _echo "[X86 Up Time   ]: ${x86_uptime}"

    if [[ -f "/var/log/wtmp" ]];then
        _echo "[X86 Last Login]: "
        _echo "${last_login}"
        _echo ""
    fi

    cmd_array=("uname -a" "cat /proc/cmdline" "cat /proc/ioports" \
               "cat /proc/iomem" "cat /proc/meminfo" \
               "cat /proc/sys/kernel/printk" \
               "find /etc -name '*-release' -print -exec cat {} \;")
    
    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        _echo "[Command]: ${cmd_array[$i]}"
        ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT}")
        _echo "${ret}"
        _echo ""
    done

}

function _show_cpu_usage {
    _banner "Show CPU Usage"

    ret=$(eval "top -b -n 1")

    _echo "${ret}"
}

function _show_grub {

    if [ ! -f "/mnt/onl/boot/grub/grub.cfg" ]; then
        return 0
    fi

    _banner "Show GRUB Info"

    grub_info=`cat /mnt/onl/boot/grub/grub.cfg`

    _echo "[GRUB Info     ]:"
    _echo "${grub_info}"

}

function _show_driver {
    _banner "Show Kernel Driver"

    cmd_array=("lsmod" \
               "cat /lib/modules/$(uname -r)/modules.builtin")

    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        _echo "[Command]: ${cmd_array[$i]}"
        ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT}")
        _echo "${ret}"
        _echo ""
    done
}

function _pre_log {
    _banner "Pre Log"

    _show_i2c_tree_bus_0
    _show_i2c_tree_bus_0
    _show_i2c_tree_bus_0
}

function _show_board_info {
    _banner "Show Board Info"

    PRODUCT_NAME=$(dmidecode -t 1 | grep "Product Name" | cut -d : -f 2 | xargs)
    HW_REV=$(dmidecode -t 2 | grep "Version" | cut -d : -f 2 | xargs)

    _echo "[Product_Name]: ${PRODUCT_NAME}"
    _echo "[Board Revision]: ${HW_REV}"
}

function _show_sys_devices {
    _banner "Show System Devices"

    local dir_path="/sys/class/gpio/"
    if [ -d "${dir_path}" ]; then
        _echo ""
        _echo "[Command]: ls /sys/class/gpio/"
        #ret=($(ls /sys/class/gpio/))
        #_echo "#${ret[*]}"
        ret=`ls -al /sys/class/gpio/`
        _echo "${ret}"
    fi

    local file_path="/sys/kernel/debug/gpio"
    if [ -f "${file_path}" ]; then
        _echo ""
        _echo "[Command]: cat ${file_path}"
        _echo "$(cat ${file_path})"
    fi

    _echo ""
    _echo "[Command]: ls /sys/bus/i2c/devices/"
    #ret=($(ls /sys/bus/i2c/devices/))
    #_echo "#${ret[*]}"
    ret=`ls -al /sys/bus/i2c/devices/`
    _echo "${ret}"

    _echo ""
    _echo "[Command]: ls -al /dev/"
    #ret=($(ls -al /dev/))
    #_echo "#${ret[*]}"
    ret=`ls -al /dev/`
    _echo "${ret}"
}

function _get_netif_name {
    onl=`uname -r | grep OpenNetworkLinux`
    if [ -z $onl ]; then
       # other NOS
       if [[ $1 == "0" ]]; then
           # sfp 0
           NET_IF="eth1"
       else
           # sfp 1
           NET_IF="eth2"
       fi
    else
       # ONL
       NET_PREFIX="enp182s0f"
       if [[ $1 == "0" ]]; then
           # sfp 0
           NET_IF="enp182s0f0"
       else
           # sfp 1
           NET_IF="enp182s0f1"
       fi       
    fi
}

function _show_bmc_info {
    _banner "Show BMC Info"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi

    cmd_array=("ipmitool mc info" "ipmitool lan print" "ipmitool sel info" \
               "ipmitool fru -v" "ipmitool power status" \
               "ipmitool channel info 0xf" "ipmitool channel info 0x1" \
               "ipmitool sol info 0x1" \
               "ipmitool mc watchdog get" "ipmitool mc info -I usb")

    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        _echo "[Command]: ${cmd_array[$i]} "
        ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT}")
        _echo "${ret}"
        _echo ""
    done
}

function _show_bmc_sensors {
    _banner "Show BMC Sensors"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi

    ret=$(eval "ipmitool sensor ${LOG_REDIRECT}")
    _echo "[Sensors]:"
    _echo "${ret}"
}

function _show_bmc_sel_raw_data {
    _banner "Show BMC SEL Raw Data"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi

    echo "    Show BMC SEL Raw Data, please wait..."

    if [ "${LOG_FILE_ENABLE}" == "1" ]; then
        _echo "[SEL RAW Data]:"
        ret=$(eval "ipmitool sel save ${LOG_FOLDER_PATH}/sel_raw_data.log > /dev/null ${LOG_REDIRECT}")
        _echo "The file is located at ${LOG_FOLDER_NAME}/sel_raw_data.log"
    else
        _echo "[SEL RAW Data]:"        
        ret=$(eval "ipmitool sel save /tmp/log/sel_raw_data.log > /dev/null ${LOG_REDIRECT}")        
        cat /tmp/log/sel_raw_data.log
        rm /tmp/log/sel_raw_data.log
    fi
}

function _show_bmc_sel_elist {
    _banner "Show BMC SEL"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi

    ret=$(eval "ipmitool sel elist ${LOG_REDIRECT}")
    _echo "[SEL Record]:"
    _echo "${ret}"
}

function _show_bmc_sel_elist_detail {
    _banner "Show BMC SEL Detail -- Abnormal Event"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi

    _echo "    Show BMC SEL details, please wait..."
    sel_id_list=""

    readarray sel_array < <(ipmitool sel elist 2> /dev/null)

    for (( i=0; i<${#sel_array[@]}; i++ ))
    do
        if [[ "${sel_array[$i]}" == *"Undetermined"* ]] ||
           [[ "${sel_array[$i]}" == *"Bus"* ]] ||
           [[ "${sel_array[$i]}" == *"CATERR"* ]] ||
           [[ "${sel_array[$i]}" == *"OEM"* ]] ; then
            _echo  "${sel_array[$i]}"
            sel_id=($(echo "${sel_array[$i]}" | awk -F" " '{print $1}'))
            sel_id_list="${sel_id_list} 0x${sel_id}"
        fi
    done

    if [ ! -z "${sel_id_list}" ]; then
        sel_detail=$(eval "ipmitool sel get ${sel_id_list} ${LOG_REDIRECT}")
    else
        sel_detail=""
    fi

    _echo "[SEL Record ID]: ${sel_id_list}"
    _echo ""
    _echo "[SEL Detail   ]:"
    _echo "${sel_detail}"
}

function _show_cpu_eeprom_i2c {
    _banner "Show CPU EEPROM"

    #first read return empty content
    cpu_eeprom=$(eval "i2cdump -y 0x${1} 0x${2} c")
    #second read return correct content
    cpu_eeprom=$(eval "i2cdump -y 0x${1} 0x${2} c")
    _echo "[CPU EEPROM]:"
    _echo "${cpu_eeprom}"
}

function _show_cpu_eeprom_sysfs {
    _banner "Show CPU EEPROM"

    cpu_eeprom=$(eval "cat ${cpu_eeprom_sysfs} ${LOG_REDIRECT} | hexdump -C")
    _echo "[CPU EEPROM]:"
    _echo "${cpu_eeprom}"
}



function _show_bmc_device_status {    
    _banner "Show BMC Device Status"

    _check_bmc_device
    find_bmc_dev=$?
    if [[ find_bmc_dev -eq ${FALSE} ]]; then
        _echo "Not support!"
        return
    fi
    # Get PSU 1
    psu1_hw_revision=$(eval     "ipmitool raw 0x34 0x16 ${bmc_psu1_id} 0x14 ${LOG_REDIRECT}")
    psu1_mfr_id=$(eval           "ipmitool raw 0x34 0x16 ${bmc_psu1_id} 0x12 ${LOG_REDIRECT}")
    psu1_data=$(eval            " ipmitool raw 0x34 0x16 ${bmc_psu1_id} ${LOG_REDIRECT}")

    _echo "[PSU 1 HW revision ]:"
    _echo " ${psu1_hw_revision}"
    _echo "[PSU 1 MFR ID ]:"
    _echo " ${psu1_mfr_id}"
    _echo "[PSU 1 data ]:"
    _echo " ${psu1_data}"

    # Get PSU 2
    psu2_hw_revision=$(eval     "ipmitool raw 0x34 0x16 ${bmc_psu2_id} 0x14 ${LOG_REDIRECT}")
    psu2_mfr_id=$(eval           "ipmitool raw 0x34 0x16 ${bmc_psu2_id} 0x12 ${LOG_REDIRECT}")
    psu2_data=$(eval            " ipmitool raw 0x34 0x16 ${bmc_psu2_id} ${LOG_REDIRECT}")

    _echo "[PSU 2 HW revision ]:"
    _echo " ${psu2_hw_revision}"
    _echo "[PSU 2 MFR ID ]:"
    _echo " ${psu2_mfr_id}"
    _echo "[PSU 2 data ]:"
    _echo " ${psu2_data}"
}

function _show_cpu_eeprom {

    if [[ -f "${cpu_eeprom_sysfs}" ]]; then
        _show_cpu_eeprom_sysfs ${cpu_eeprom_sysfs}
    else
        _show_cpu_eeprom_i2c ${cpu_eeprom_bus_id} ${cpu_eeprom_i2c_addr}
    fi
}

function _show_psu_status_cpld_sysfs {
    _banner "Show PSU Status (CPLD)"

    # Read PSU Status
    _check_filepath "${psu1_present_sysfs}"
    _check_filepath "${psu1_power_good_sysfs}"
    _check_filepath "${psu2_present_sysfs}"
    _check_filepath "${psu2_power_good_sysfs}"

    # Read PSU1 Present Status (1: psu present, 0: psu absent)
    psu1_present=$(eval "cat ${psu1_present_sysfs} ${LOG_REDIRECT}")

    # Read PSU1 Power Good Status (1: power good, 0: not providing power)
    psu1_power_good=$(eval "cat ${psu1_power_good_sysfs} ${LOG_REDIRECT}")

    # Read PSU2 Present Status (1: psu present, 0: psu absent)
    psu2_present=$(eval "cat ${psu2_present_sysfs} ${LOG_REDIRECT}")

    # Read PSU2 Power Good Status (1: power good, 0: not providing power)
    psu2_power_good=$(eval "cat ${psu2_power_good_sysfs} ${LOG_REDIRECT}")

    _echo "[PSU1 Present Status (L)]: ${psu1_present}"
    _echo "[PSU1 Power Good Status]: ${psu1_power_good}"
    _echo "[PSU2 Present Status (L)]: ${psu2_present}"
    _echo "[PSU2 Power Good Status]: ${psu2_power_good}"
}

function _show_psu_status_cpld {
    _show_psu_status_cpld_sysfs
}

function _show_sfp_port_status_sysfs {
    _banner "Show SFP Port Status / EEPROM"
    echo "    Show SFP Port Status / EEPROM, please wait..."

    if [ ${support_sfp} -eq 0 ]; then
        _echo "Not support!"
        return
    fi

    for (( i=0; i<${#sfp_port_array[@]}; i++ ))
    do
        # Module SFP Port RX LOS (0:Undetected, 1:Detected)
        port_rx_los=""
        sysfs=$(_sfp_get_rx_los_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_rx_los=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi

        # Module SFP Port Present Status (1:Present, 0:Absence)
        port_present=""
        sysfs=$(_sfp_get_present_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_present=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi  

        # Module SFP Port TX FAULT (0:Undetected, 1:Detected)
        port_tx_fault=""
        sysfs=$(_sfp_get_tx_fault_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_tx_fault=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi  

        # Module SFP Port TX DISABLE (0:Disabled, 1:Enabled)
        port_tx_disable=""
        sysfs=$(_sfp_get_tx_disable_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_tx_disable=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi 


        # Module SFP Port Dump EEPROM
        if [ "${port_present}" == "1" ]; then
            sysfs=$(_sfp_get_eeprom_func "$i")
            _check_filepath $sysfs
            port_eeprom_p0_1st=$(eval  "dd if=$sysfs bs=128 count=2 skip=0  status=none ${LOG_REDIRECT} | hexdump -C")
            port_eeprom_p0_2nd=$(eval  "dd if=$sysfs bs=128 count=2 skip=0  status=none ${LOG_REDIRECT} | hexdump -C")
            if [ -z "$port_eeprom_p0_1st" ]; then
                port_eeprom_p0_1st="ERROR!!! The result is empty. It should read failed ($sysfs)!!"
            fi

            # Full EEPROM Log
            if [ "${LOG_FILE_ENABLE}" == "1" ]; then
                hexdump -C $sysfs > ${LOG_FOLDER_PATH}/sfp_port${sfp_port_array[i]}_eeprom.log 2>&1
            fi
        else
            port_eeprom_p0_1st="N/A"
            port_eeprom_p0_2nd="N/A"
        fi

        if [ ! -z ${port_rx_los} ]; then
            _echo "[SFP Port${sfp_port_array[i]} RX LOS]: ${port_rx_los}"
        fi

        if [ ! -z ${port_present} ]; then
            _echo "[SFP Port${sfp_port_array[i]} Module Present]: ${port_present}"
        fi

        if [ ! -z ${port_tx_fault} ]; then
            _echo "[SFP Port${sfp_port_array[i]} TX FAULT]: ${port_tx_fault}"
        fi

        if [ ! -z ${port_tx_disable} ]; then
            _echo "[SFP Port${sfp_port_array[i]} TX DISABLE]: ${port_tx_disable}"
        fi

        _echo "[Port${sfp_port_array[i]} EEPROM Page0-0(1st)]:"
        _echo "${port_eeprom_p0_1st}"
        _echo "[Port${sfp_port_array[i]} EEPROM Page0-0(2nd)]:"
        _echo "${port_eeprom_p0_2nd}"
        _echo ""
    done
}

function _show_sfp_port_status {
    _show_sfp_port_status_sysfs
}

function _show_qsfpdd_port_status_sysfs {
    _banner "Show QSFPDD Port Status / EEPROM"
    echo "    Show QSFPDD Port Status / EEPROM, please wait..."                                                                                    

    if [ ${support_qsfpdd} -eq 0 ]; then
        _echo "Not support!"
        return
    fi

    for (( i=0; i<${#qsfp_port_array[@]}; i++ ))
    do
        # Module QSFPDD Port Present Status (1: Present, 0:Absence)
        port_module_present=""
        sysfs=$(_qsfpdd_get_present_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_module_present=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi

        # Module QSFPDD Port Get Low Power Mode Status (0: Normal Power Mode, 1:Low Power Mode)
        port_lp_mode=""
        sysfs=$(_qsfpdd_get_lp_mode_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_lp_mode=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi

        # Module QSFPDD Port Reset Status (0:Reset, 1:Normal)
        port_reset=""
        sysfs=$(_qsfpdd_get_reset_func "$i")
        if [ -n "$sysfs" ] && [ -f "$sysfs" ]; then
            port_reset=$(eval "cat $sysfs ${LOG_REDIRECT}")
        fi

        # Module QSFPDD Port Dump EEPROM
        if [ "${port_module_present}" == "1" ]; then
            sysfs=$(_qsfpdd_get_eeprom_func "$i")
            _check_filepath $sysfs
            port_eeprom_p0_1st=$(eval  "dd if=$sysfs bs=128 count=2 skip=0  status=none ${LOG_REDIRECT} | hexdump -C")
            port_eeprom_p0_2nd=$(eval  "dd if=$sysfs bs=128 count=2 skip=0  status=none ${LOG_REDIRECT} | hexdump -C")
            port_eeprom_p17_1st=$(eval "dd if=$sysfs bs=128 count=1 skip=18 status=none ${LOG_REDIRECT} | hexdump -C")
            port_eeprom_p17_2nd=$(eval "dd if=$sysfs bs=128 count=1 skip=18 status=none ${LOG_REDIRECT} | hexdump -C")
            port_eeprom_p18=$(eval     "dd if=$sysfs bs=128 count=1 skip=19 status=none ${LOG_REDIRECT} | hexdump -C")
            if [ -z "$port_eeprom_p0_1st" ]; then
                port_eeprom_p0_1st="ERROR!!! The result is empty. It should read failed ($sysfs)!!"
            fi

            # Full EEPROM Log
            if [ "${LOG_FILE_ENABLE}" == "1" ]; then
                hexdump -C $sysfs > ${LOG_FOLDER_PATH}/qsfpdd_port${qsfp_port_array[i]}_eeprom.log 2>&1
            fi
        else
            port_eeprom_p0_1st="N/A"
            port_eeprom_p0_2nd="N/A"
            port_eeprom_p17_1st="N/A"
            port_eeprom_p17_2nd="N/A"
            port_eeprom_p18="N/A"
        fi



        if [ ! -z ${port_module_present} ]; then
            _echo "[QSFPDD Port${qsfp_port_array[i]} Module Present ]: ${port_module_present}"
        fi
        
        if [ ! -z ${port_lp_mode} ]; then
            _echo "[QSFPDD Port${qsfp_port_array[i]} Low Power Mode]: ${port_lp_mode}"
        fi

        if [ ! -z ${port_reset} ]; then
            _echo "[QSFPDD Port${qsfp_port_array[i]} Reset Status]: ${port_reset}"
        fi
        _echo "[Port${qsfp_port_array[i]} EEPROM Page0-0(1st)]:"
        _echo "${port_eeprom_p0_1st}"
        _echo "[Port${qsfp_port_array[i]} EEPROM Page0-0(2nd)]:"
        _echo "${port_eeprom_p0_2nd}"
        _echo "[Port${qsfp_port_array[i]} EEPROM Page17 (1st)]:"
        _echo "${port_eeprom_p17_1st}"
        _echo "[Port${qsfp_port_array[i]} EEPROM Page17 (2nd)]:"
        _echo "${port_eeprom_p17_2nd}"
        _echo "[Port${qsfp_port_array[i]} EEPROM Page18      ]:"
        _echo "${port_eeprom_p18}"
        _echo ""
    done
}

function _show_qsfpdd_port_status {
    _show_qsfpdd_port_status_sysfs
}

function _show_cpu_temperature_sysfs {
    _banner "show CPU Temperature"

    for (( i=0; i<${#cpu_temp_bus_id_array[@]}; i++ ))
    do
        if [ -f "/sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_input" ]; then
            _check_filepath "/sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_input"
            _check_filepath "/sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_max"
            _check_filepath "/sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_crit"
            temp_input=$(eval "cat /sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_input ${LOG_REDIRECT}")
            temp_max=$(eval "cat /sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_max ${LOG_REDIRECT}")
            temp_crit=$(eval "cat /sys/devices/platform/coretemp.0/hwmon/${cpu_temp_hwmon}/temp${cpu_temp_bus_id_array[i]}_crit ${LOG_REDIRECT}")
        elif [ -f "/sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_input" ]; then
            _check_filepath "/sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_input"
            _check_filepath "/sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_max"
            _check_filepath "/sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_crit"
            temp_input=$(eval "cat /sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_input ${LOG_REDIRECT}")
            temp_max=$(eval "cat /sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_max ${LOG_REDIRECT}")
            temp_crit=$(eval "cat /sys/devices/platform/coretemp.0/temp${cpu_temp_bus_id_array[i]}_crit ${LOG_REDIRECT}")
        else
            _echo "sysfs of CPU core temperature not found!!!"
        fi

        _echo "[CPU Core Temp${cpu_temp_bus_id_array[i]} Input   ]: ${temp_input}"
        _echo "[CPU Core Temp${cpu_temp_bus_id_array[i]} Max     ]: ${temp_max}"
        _echo "[CPU Core Temp${cpu_temp_bus_id_array[i]} Crit    ]: ${temp_crit}"
        _echo ""
    done
}

function _show_cpu_temperature {
    _show_cpu_temperature_sysfs
}

function _show_system_led_sysfs {
    _banner "Show System LED"

    for (( i=0; i<${#sys_led_array[@]}; i++ ))
    do
        sysfs=${sys_led_sysfs[i]}
        _check_filepath $sysfs

        system_led=$(eval "cat $sysfs ${LOG_REDIRECT}")
        _echo "[System LED ${sys_led_array[i]^^}]: ${system_led}"
    done
}

function _show_system_led {
    _show_system_led_sysfs
}

function _show_beacon_led_sysfs {
    _banner "Show Beacon LED"

    sysfs=${sys_beacon_led_sysfs}

    if [[ ! -f ${sysfs} ]]; then
        _echo "Not support!"
        return
    fi
    beacon_led=$(eval "cat $sysfs ${LOG_REDIRECT}")
    _echo "[Beacon LED]: ${beacon_led}"
}

function _show_beacon_led {
    _show_beacon_led_sysfs
}

function _show_version {

    _banner "Show Version"

    ret=$(eval "onlpdump -x")

    _echo "${ret}"
}

function _show_i2c_tree_bus_0 {
    _banner "Show I2C Tree Bus 0"

    ret=$(eval "i2cdetect -y 0 ${LOG_REDIRECT}")

    _echo "[I2C Tree]:"
    _echo "${ret}"
}

function _show_i2c_mux_devices {
    local chip_addr=$1
    local channel_num=$2
    local chip_dev_desc=$3
    local i=0;

    if [ -z "${chip_addr}" ] || [ -z "${channel_num}" ] || [ -z "${chip_dev_desc}" ]; then
        _echo "ERROR: parameter cannot be empty!!!"
        exit 99
    fi

    _check_i2c_device "$chip_addr"
    ret=$?
    if [ "$ret" == "0" ]; then
        _echo "TCA9548 Mux ${chip_dev_desc}"
        _echo "---------------------------------------------------"
        for (( i=0; i<${channel_num}; i++ ))
        do
            _echo "TCA9548 Mux ${chip_dev_desc} - Channel ${i}"
            # open mux channel
            i2cset -y 0 ${chip_addr} $(( 2 ** ${i} ))
            # dump i2c tree
            ret=$(eval "i2cdetect -y 0 ${LOG_REDIRECT}")
            _echo "${ret}"
            # close mux channel
            i2cset -y 0 ${chip_addr} 0x0 
            _echo ""
        done
    fi

}

function _show_i2c_tree_bus_mux_i2c {

    if [ "${OPT_BYPASS_I2C_COMMAND}" == "${TRUE}" ]; then
        _banner "Show I2C Tree Bus MUX (I2C) (Bypass)"
        return
    fi

    _banner "Show I2C Tree Bus MUX (I2C)"

    local i=0
    local chip_addr1=""
    local chip_addr2=""
    local chip_addr3=""
    local chip_addr1_chann=""
    local chip_addr2_chann=""

}

function _show_i2c_tree {
    _banner "Show I2C Tree"

    _show_i2c_tree_bus_0

    if [ "${BSP_INIT_FLAG}" == "1" ]; then
        _echo "TBD"
    else
        _show_i2c_tree_bus_mux_i2c
    fi

    _show_i2c_tree_bus_0
}

function _show_i2c_device_info {
    _banner "Show I2C Device Info"

    ret=`i2cdump -y -f 0 0x77 b`
    _echo "[I2C Device 0x77]:"
    _echo "${ret}"
    _echo ""

    local pca954x_device_id=("")

    for ((i=0;i<5;i++))
    do
        _echo "[DEV PCA9548 (${i})]"
        for (( j=0; j<${#pca954x_device_id[@]}; j++ ))
        do
            ret=`i2cget -f -y 0 ${pca954x_device_id[$j]}`
            _echo "[I2C Device ${pca954x_device_id[$j]}]: $ret"
        done
        sleep 0.4
    done
}

function _show_usb_info {
    _banner "Show USB Info"

    _echo "[Command]: lsusb -t"
    ret=$(eval "lsusb -t ${LOG_REDIRECT}")
    _echo "${ret}"
    _echo ""

    _echo "[Command]: lsusb -v"
    ret=$(eval "lsusb -v ${LOG_REDIRECT}")
    _echo "${ret}"
    _echo ""

    _echo "[Command]: grep 046b /sys/bus/usb/devices/*/idVendor"
    ret=$(eval "grep 046b /sys/bus/usb/devices/*/idVendor ${LOG_REDIRECT}")
    _echo "${ret}"
    _echo ""

    # check usb auth
    _echo "[USB Port Authentication]: "

    for (( i=0; i<${#usb_auth_file_array[@]}; i++ ))
    do
        _check_filepath "${usb_auth_file_array[$i]}"
        if [ -f "${usb_auth_file_array[$i]}" ]; then
            ret=$(eval "cat ${usb_auth_file_array[$i]} ${LOG_REDIRECT}")
            _echo "${usb_auth_file_array[$i]}: $ret"
        else
            _echo "${usb_auth_file_array[$i]}: -1"
        fi
    done
}

function _show_ioport {
    _banner "Show ioport (LPC)"

    _echo "Not support!"
    return
}

function _show_onlpdump {
    _banner "Show onlpdump"

    which onlpdump > /dev/null 2>&1
    ret_onlpdump=$?

    if [ ${ret_onlpdump} -eq 0 ]; then
        cmd_array=("onlpdump -d" \
                   "onlpdump -s" \
                   "onlpdump -r" \
                   "onlpdump -e" \
                   "onlpdump -o" \
                   "onlpdump -x" \
                   "onlpdump -i" \
                   "onlpdump -p" \
                   "onlpdump -S")
        for (( i=0; i<${#cmd_array[@]}; i++ ))
        do
            _echo "[Command]: ${cmd_array[$i]}"
            ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT} | tr -d '\0'")
            _echo "${ret}"
            _echo ""
        done
    else
        _echo "Not support!"
    fi
}

function _show_onlps {
    _banner "Show onlps"

    which onlps > /dev/null 2>&1
    ret_onlps=$?

    if [ ${ret_onlps} -eq 0 ]; then
        cmd_array=("onlps chassis onie show -" \
                   "onlps chassis asset show -" \
                   "onlps chassis env -" \
                   "onlps sfp inventory -" \
                   "onlps sfp bitmaps -" \
                   "onlps chassis debug show -")
        for (( i=0; i<${#cmd_array[@]}; i++ ))
        do
            _echo "[Command]: ${cmd_array[$i]}"
            ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT} | tr -d '\0'")
            _echo "${ret}"
            _echo ""
        done
    else
        _echo "Not support!"
    fi
}

#require skld cpu cpld 1.12.016 and later
function _show_cpld_error_log {
    _banner "Show CPLD Error Log"

    _echo "Not support!"
    return
}

# Note: In order to prevent affecting MCE mechanism, 
#       the function will not clear the 0x425 and 0x429 registers at step 1.1/1.2,
#       and only use to show the current correctable error count.
function _show_memory_correctable_error_count {
    _banner "Show Memory Correctable Error Count"

    which rdmsr > /dev/null 2>&1
    ret_rdmsr=$?
    which wrmsr > /dev/null 2>&1
    ret_wrmsr=$?

    if [ ${ret_rdmsr} -eq 0 ] && [ ${ret_wrmsr} -eq 0 ]; then
        ERROR_COUNT_THREASHOLD=12438
        modprobe msr

        # Step 0.1: Before clear the register, dump the correctable error count in channel 0 bank 9
        reg_c0_str=`rdmsr -p0 0x425 2> /dev/null`
        if [ "${reg_c0_str}" == "" ]; then
            reg_c0_str="0"
        fi
        reg_c0_value=`printf "%u\n" 0x${reg_c0_str}`
        # CORRECTED_ERR_COUNT bit[52:38]
        error_count_c0=$(((reg_c0_value >> 38) & 0x7FFF))
        _echo "[Ori_C0_Error_Count]: ${error_count_c0}"

        # Step 0.2: Before clear the register, dump the correctable error count in channel 1 bank 10
        reg_c1_str=`rdmsr -p0 0x429 2> /dev/null`
        if [ "${reg_c1_str}" == "" ]; then
            reg_c1_str="0"
        fi
        reg_c1_value=`printf "%u\n" 0x${reg_c1_str}`
        # CORRECTED_ERR_COUNT bit[52:38]
        error_count_c1=$(((reg_c1_value >> 38) & 0x7FFF))
        _echo "[Ori_C1_Error_Count]: ${error_count_c1}"

        # Step 1.1: clear correctable error count in channel 0 bank 9
        #wrmsr -p0 0x425 0x0

        # Step 1.2: clear correctable error count in channel 1 bank 10
        #wrmsr -p0 0x429 0x0

        # Step 2: wait 2 seconds
        sleep 2

        # Step 3.1: Read correctable error count in channel 0 bank 9
        reg_c0_str=`rdmsr -p0 0x425 2> /dev/null`
        if [ "${reg_c0_str}" == "" ]; then
            reg_c0_str="0"
        fi
        reg_c0_value=`printf "%u\n" 0x${reg_c0_str}`
        # CORRECTED_ERR_COUNT bit[52:38]
        error_count_c0=$(((reg_c0_value >> 38) & 0x7FFF))
        if [ ${error_count_c0} -gt ${ERROR_COUNT_THREASHOLD} ]; then
            _echo "[ERROR] Channel 0 Bank  9 Register Value: 0x${reg_c0_str}, Error Count: ${error_count_c0}"
        else
            _echo "[Info] Channel 0 Bank  9 Register Value: 0x${reg_c0_str}, Error Count: ${error_count_c0}"
        fi

        # Step 3.2: Read correctable error count in channel 1 bank 10
        reg_c1_str=`rdmsr -p0 0x429 2> /dev/null`
        if [ "${reg_c1_str}" == "" ]; then
            reg_c1_str="0"
        fi
        reg_c1_value=`printf "%u\n" 0x${reg_c1_str}`
        # CORRECTED_ERR_COUNT bit[52:38]
        error_count_c1=$(((reg_c1_value >> 38) & 0x7FFF))
        if [ ${error_count_c1} -gt ${ERROR_COUNT_THREASHOLD} ]; then
            _echo "[ERROR] Channel 1 Bank 10 Register Value: 0x${reg_c1_str}, Error Count: ${error_count_c1}"
        else
            _echo "[Info] Channel 1 Bank 10 Register Value: 0x${reg_c1_str}, Error Count: ${error_count_c1}"
        fi
    else
        _echo "Not support! Please install msr-tools to enble this function."
    fi
}

function _show_scsi_device_info {
    _banner "Show SCSI Device Info"

    scsi_device_info=$(eval "cat /proc/scsi/sg/device_strs ${LOG_REDIRECT}")
    _echo "[SCSI Device Info]: "
    _echo "${scsi_device_info}"
    _echo ""
}

function _show_onie_upgrade_info {
    _banner "Show ONIE Upgrade Info"

    if [ -d "/sys/firmware/efi" ]; then
        if [ ! -d "/mnt/onie-boot/" ]; then
            mkdir /mnt/onie-boot
        fi

        mount LABEL=ONIE-BOOT /mnt/onie-boot/
        onie_show_version=`/mnt/onie-boot/onie/tools/bin/onie-version| tr -d '\000'`
        onie_show_pending=`/mnt/onie-boot/onie/tools/bin/onie-fwpkg show-pending| tr -d '\000'`
        onie_show_result=`/mnt/onie-boot/onie/tools/bin/onie-fwpkg show-results| tr -d '\000'`
        onie_show_log=`/mnt/onie-boot/onie/tools/bin/onie-fwpkg show-log| tr -d '\000'`
        umount /mnt/onie-boot/

        _echo "[ONIE Show Version]:"
        _echo "${onie_show_version}"
        _echo ""
        _echo "[ONIE Show Pending]:"
        _echo "${onie_show_pending}"
        _echo ""
        _echo "[ONIE Show Result ]:"
        _echo "${onie_show_result}"
        _echo ""
        _echo "[ONIE Show Log    ]:"
        _echo "${onie_show_log}"
    else
        _echo "BIOS is in Legacy Mode!!!!!"
    fi
}

function _show_disk_info {
    _banner "Show Disk Info"
   
    cmd_array=("lsblk" \
               "parted -l /dev/sda" \
               "fdisk -l /dev/sda" \
               "find /sys/fs/ -name errors_count -print -exec cat {} \;" \
               "find /sys/fs/ -name first_error_time -print -exec cat {} \; -exec echo '' \;" \
               "find /sys/fs/ -name last_error_time -print -exec cat {} \; -exec echo '' \;" \
               "df -h")

    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        _echo "[Command]: ${cmd_array[$i]}"
        ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT}")
        _echo "${ret}"
        _echo ""
    done

    # check smartctl command
    cmd="smartctl -a /dev/sda"
    ret=`which smartctl`
    if [ ! $? -eq 0 ]; then
        _echo "[command]: ($cmd) not found (SKIP)!!"
    else
        ret=$(eval "$cmd ${LOG_REDIRECT}")
        _echo "[command]: $cmd"
        _echo "${ret}"
    fi
}

function _show_lspci {
    _banner "Show lspci Info"

    ret=`lspci`
    _echo "${ret}"
    _echo ""
}

function _show_lspci_detail {
    _banner "Show lspci Detail Info"

    ret=$(eval "lspci -xxxx -vvv ${LOG_REDIRECT}")
    _echo "${ret}"
}

function _show_proc_interrupt {
    _banner "Show Proc Interrupts"

    for i in {1..5};
    do
        ret=$(eval "cat /proc/interrupts ${LOG_REDIRECT}")
        _echo "[Proc Interrupts ($i)]:"
        _echo "${ret}"
        _echo ""
        sleep 1
    done
}

function _show_ipmi_info {
    _banner "Show IPMI Info"

    ipmi_folder="/proc/ipmi/0/"

    if [ -d "${ipmi_folder}" ]; then
        ipmi_file_array=($(ls ${ipmi_folder}))
        for (( i=0; i<${#ipmi_file_array[@]}; i++ ))           
        do
            _echo "[Command]: cat ${ipmi_folder}/${ipmi_file_array[$i]} "
            ret=$(eval "cat "${ipmi_folder}/${ipmi_file_array[$i]}" ${LOG_REDIRECT}")
            _echo "${ret}"
            _echo ""
        done
    else
        _echo "Warning, folder not found (${ipmi_folder})!!!"
    fi

    _echo "[Command]: lsmod | grep ipmi "
    ret=`lsmod | grep ipmi`
    _echo "${ret}"
}

function _show_bios_info {
    _banner "Show BIOS Info"

    cmd_array=("dmidecode -t 0" \
               "dmidecode -t 1" \
               "dmidecode -t 2" \
               "dmidecode -t 3")
    
    for (( i=0; i<${#cmd_array[@]}; i++ ))
    do
        _echo "[Command]: ${cmd_array[$i]} "
        ret=$(eval "${cmd_array[$i]} ${LOG_REDIRECT}")
        _echo "${ret}"
        _echo ""
    done
}

function _show_bios_flash {
    _banner "Show BIOS Flash"

    if [ ${support_bios_flash} -eq 0 ]; then
        _echo "Not support!"
        return
    fi

    ret=$(eval "cat ${bios_boot_flash_sysfs} ${LOG_REDIRECT}")

    if [[ ! -z "${ret}" ]]; then
        if [[ ${ret} -eq 1 ]]; then
            _echo "Master"
        elif [[ ${ret} -eq 2 ]]; then
            _echo "Slave"
        else
            _echo "Not defined!"
        fi
    else
        _echo "Not support!"
    fi
}

function _show_dmesg {
    _banner "Show Dmesg"

    ret=$(eval "dmesg ${LOG_REDIRECT}")
    _echo "${ret}"
}

function _additional_log_collection {
    _banner "Additional Log Collection"

    if [ -z "${LOG_FOLDER_PATH}" ] || [ ! -d "${LOG_FOLDER_PATH}" ]; then
        _echo "LOG_FOLDER_PATH (${LOG_FOLDER_PATH}) not found!!!"
        _echo "do nothing..."
    else
        #_echo "copy /var/log/syslog* to ${LOG_FOLDER_PATH}"
        #cp /var/log/syslog*  "${LOG_FOLDER_PATH}"

        if [ -f "/var/log/kern.log" ]; then
            _echo "copy /var/log/kern.log* to ${LOG_FOLDER_PATH}"
            cp /var/log/kern.log*  "${LOG_FOLDER_PATH}"
        fi
        
        if [ -f "/var/log/dmesg" ]; then
            _echo "copy /var/log/dmesg* to ${LOG_FOLDER_PATH}"
            cp /var/log/dmesg*  "${LOG_FOLDER_PATH}"
        fi
    fi
}

function _show_time {
    _banner "Show Execution Time"
    end_time=$(date +%s)
    elapsed_time=$(( end_time - start_time ))

    ret=`date -d @${start_time}`
    _echo "[Start Time ] ${ret}"

    ret=`date -d @${end_time}`
    _echo "[End Time   ] ${ret}"

    _echo "[Elapse Time] ${elapsed_time} seconds"
}

function _compression {
    _banner "Compression"

    if [ ! -z "${LOG_FOLDER_PATH}" ] && [ -d "${LOG_FOLDER_PATH}" ]; then
        cd "${LOG_FOLDER_ROOT}"
        tar -zcf "${LOG_FOLDER_NAME}".tgz "${LOG_FOLDER_NAME}"

        echo "The tarball is ready at ${LOG_FOLDER_ROOT}/${LOG_FOLDER_NAME}.tgz"
        _echo "The tarball is ready at ${LOG_FOLDER_ROOT}/${LOG_FOLDER_NAME}.tgz"
    fi
}

usage() {
    local f=$(basename "$0")
    echo ""
    echo "Usage:"
    echo "    $f [-b] [-d D_DIR] [-h] [-i identifier] [-v]"
    echo "Description:"
    echo "  -b                bypass i2c command (required when NOS vendor use their own platform bsp to control i2c devices)"    
    echo "  -d                specify D_DIR as log destination instead of default path /tmp/log"
    echo "  -i                insert an identifier in the log file name"
    echo "  -m                specify the model name"
    echo "  -v                show tech support script version"
    echo "Example:"
    echo "    $f -d /var/log"
    echo "    $f -i identifier"
    echo "    $f -v"
    exit -1
}

function _getopts {
    local OPTSTRING=":bd:fi:m:v"
    # default log dir
    local log_folder_root=$DEFAULT_LOG_FOLDER_ROOT
    local identifier=""

    while getopts ${OPTSTRING} opt; do
        case ${opt} in
            b)
              OPT_BYPASS_I2C_COMMAND=${TRUE}
              ;;
            d)
              log_folder_root=${OPTARG}
              ;;
            f)
              LOG_FAST=${TRUE}
              ;;
            i)
              identifier=${OPTARG}
              ;;
            v)
              _show_ts_version
              exit 0
              ;;
            ?)
              echo "Invalid option: -${OPTARG}."
              usage
              ;;
        esac
    done

    MODEL=$(eval "ls /mnt/onl/boot/ | grep cpio.gz | cut -d . -f 1")

    if [ -n "${MODEL}" ]; then
        LIB_PATH="/lib/platform-config/${MODEL}/onl/tech_support/show_platform_log_lib.sh"

        if [[ -f ${LIB_PATH} ]]; then
            source ${LIB_PATH}
        else
            echo "Lib script not found!"
            exit 1
        fi
    else
        echo "Model not found !"
        exit 1
    fi


    LOG_FOLDER_ROOT=${log_folder_root}
    if [ -z "$identifier" ]; then
        LOG_FOLDER_NAME="${DEFAULT_LOG_FOLDER_NAME}"
        LOG_FILE_NAME="${DEFAULT_LOG_FILE_NAME}"
    else
        LOG_FOLDER_NAME="log_platform_${identifier}_${DATESTR}"
        LOG_FILE_NAME="log_platform_${identifier}_${DATESTR}.log"
    fi
    LOG_FOLDER_PATH="${LOG_FOLDER_ROOT}/${LOG_FOLDER_NAME}"
    LOG_FILE_PATH="${LOG_FOLDER_PATH}/${LOG_FILE_NAME}"
    LOG_REDIRECT="2>> $LOG_FILE_PATH"
}

function _main {
    echo "The script will take a few minutes, please wait..."
    ### Basic system info ###
    _check_env
    _pkg_version
    _pre_log
    _show_board_info
    _show_version
    _show_system_info
    _show_cpu_usage
    _show_grub
    _show_onie_upgrade_info
    _show_proc_interrupt
    _show_bios_info
    _show_bios_flash
    _show_dmesg
    _additional_log_collection

    ### HW related info ###
    #_show_i2c_tree
    #_show_i2c_device_info
    _show_sys_devices
    _show_cpu_eeprom
    _show_usb_info
    _show_scsi_device_info
    _show_disk_info
    _show_lspci
    _show_lspci_detail
    _show_ioport
    _show_cpld_error_log

    ### sysfs/driver info ###
    _show_driver
    _show_psu_status_cpld
    _show_sfp_port_status
    _show_qsfpdd_port_status
    _show_cpu_temperature
    _show_system_led
    _show_beacon_led

    ### ONLP info ###
    _show_onlpdump
    _show_onlps

    ### BMC info ###
    _show_bmc_info
    _show_bmc_sensors
    _show_bmc_device_status
    _show_bmc_sel_raw_data
    _show_bmc_sel_elist
    _show_bmc_sel_elist_detail

    ### final action ###
    _show_time
    _compression

    echo "#   The tech-support collection is completed. Please share the tech support log file."
}

_getopts $@
_main
