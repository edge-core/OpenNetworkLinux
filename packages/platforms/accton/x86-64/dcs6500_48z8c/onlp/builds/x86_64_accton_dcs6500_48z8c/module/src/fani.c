/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2014 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 * Fan Platform Implementation Defaults.
 *
 ***********************************************************/
#include <onlplib/i2c.h>
#include <onlp/platformi/fani.h>
#include "platform_lib.h"

#define PSU_PREFIX_PATH  "/sys/bus/i2c/devices/"

/*
Although in HW spec.
    Offset 0x43 FAN1_FRONT_MAX_SET_SPEED     0xDC = 220
    Offset 0x4B FAN1_REAR_MAX_SET_SPEED      0xB6 = 182
But we refer to muxi's fani.c.
*/
#define MAX_FRONT_FAN_SPEED     30000
#define MAX_REAR_FAN_SPEED      25000
#define MAX_PSU_FAN_SPEED       23000

#define XSTR(s) STR(s)
#define STR(s) #s

enum fan_id {
    FAN_BOX1_FRONT_1 = 1,
    FAN_BOX1_FRONT_2,
    FAN_BOX2_FRONT_3,
    FAN_BOX2_FRONT_4,
    FAN_BOX1_REAR_1,
    FAN_BOX1_REAR_2,
    FAN_BOX2_REAR_3,
    FAN_BOX2_REAR_4,
	FAN_1_ON_PSU_1,
	FAN_1_ON_PSU_2
};

#define CHASSIS_FAN_INFO(boxid, location, fid) \
{ \
   { ONLP_FAN_ID_CREATE(FAN_BOX##boxid##_##location##_##fid), "Chassis Fan - FAN_BOX" XSTR(boxid) "_" XSTR(location) " " XSTR(fid), 0 },         0x0,\
    ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,\
    0,\
    0,\
    ONLP_FAN_MODE_INVALID,\
}

#define PSU_FAN_INFO(pid, fid) \
    { \
        { ONLP_FAN_ID_CREATE(FAN_##fid##_ON_PSU_##pid), "PSU "#pid" - Fan "#fid, 0 },\
        0x0,\
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,\
        0,\
        0,\
        ONLP_FAN_MODE_INVALID,\
    }

/* Static fan information */
onlp_fan_info_t finfo[] = {
    { }, /* Not used */
    CHASSIS_FAN_INFO(1,FRONT,1),
    CHASSIS_FAN_INFO(1,FRONT,2),
    CHASSIS_FAN_INFO(2,FRONT,3),
    CHASSIS_FAN_INFO(2,FRONT,4),
    CHASSIS_FAN_INFO(1,REAR,1),
    CHASSIS_FAN_INFO(1,REAR,2),
    CHASSIS_FAN_INFO(2,REAR,3),
    CHASSIS_FAN_INFO(2,REAR,4),
    PSU_FAN_INFO(1,1),
    PSU_FAN_INFO(2,1)
    };

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_FAN(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

static int
_onlp_fani_info_get_fan_on_psu(int pid, onlp_fan_info_t* info)
{
	int val = 0;

	info->status |= ONLP_FAN_STATUS_PRESENT;

    /* get fan direction */
    info->status |= ONLP_FAN_STATUS_B2F;

    /* get fan fault status
     */
    if (psu_pmbus_info_get(pid, "psu_fan1_fault", &val) == ONLP_STATUS_OK) {
        info->status |= (val > 0) ? ONLP_FAN_STATUS_FAILED : 0;
    }

    /* get fan speed
     */
    if (psu_pmbus_info_get(pid, "psu_fan1_speed_rpm", &val) == ONLP_STATUS_OK) {
        info->rpm = val;
	    info->percentage = (info->rpm * 100) / MAX_PSU_FAN_SPEED;	    
    }

    return ONLP_STATUS_OK;
}

 static int
 _onlp_fani_info_get_fan(int fid, onlp_fan_info_t* info)
 {
     int rc = ONLP_STATUS_OK;
     int   value;
     char  path[64] = {0};
     char  path_front[64] = {0};
     char  path_rear[64] = {0};
 
     /* get fan present status  */
 
     switch (fid)
     {
         case FAN_BOX1_FRONT_1:
         case FAN_BOX1_FRONT_2:
         case FAN_BOX1_REAR_1:
         case FAN_BOX1_REAR_2:
             sprintf(path, "%s""fan_present_%d", CPLD_NODE_PATH, 1);
             break;
         case FAN_BOX2_FRONT_3:
         case FAN_BOX2_FRONT_4:
         case FAN_BOX2_REAR_3:
         case FAN_BOX2_REAR_4:
             sprintf(path, "%s""fan_present_%d", CPLD_NODE_PATH, 2);
             break;
         default:
            rc = ONLP_STATUS_E_INVALID;
            break;
     }
 
     DEBUG_PRINT("Fan(%d), present path = (%s)", fid, path);
     
     if (onlp_file_read_int(&value, path) < 0) {
         AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path);
         return ONLP_STATUS_E_INTERNAL;
     }

     info->status |= value ? ONLP_FAN_STATUS_PRESENT : 0;

     /* get fan direction (both : the same)
      */
     switch (fid)
     {
         case FAN_BOX1_FRONT_1:
         case FAN_BOX1_FRONT_2:
         case FAN_BOX1_REAR_1:
         case FAN_BOX1_REAR_2:
             sprintf(path, "%s""fan_direction_%d", CPLD_NODE_PATH, 1);
             break;
         case FAN_BOX2_FRONT_3:
         case FAN_BOX2_FRONT_4:
         case FAN_BOX2_REAR_3:
         case FAN_BOX2_REAR_4:
             sprintf(path, "%s""fan_direction_%d", CPLD_NODE_PATH, 2);
             break;
         default:
             rc = ONLP_STATUS_E_INVALID;
             break;
     }
 
     DEBUG_PRINT("Fan(%d), direction path = (%s)", fid, path);
     
     if (onlp_file_read_int(&value, path) < 0) {
         AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path);
         return ONLP_STATUS_E_INTERNAL;
     }
 
     /* F2B has register value 0 as per x86-64-accton-dcs6500-48z8c-fan.c */
     info->status |= value ? ONLP_FAN_STATUS_B2F : ONLP_FAN_STATUS_F2B;
 
     /* get front fan speed
      */
    switch (fid)
     {
        case FAN_BOX1_FRONT_1:
            sprintf(path_front, "%s""fan_front_speed_rpm_%d", CPLD_NODE_PATH, 1);
            break;
        case FAN_BOX1_REAR_1:
            sprintf(path_rear, "%s""fan_rear_speed_rpm_%d", CPLD_NODE_PATH, 1);
            break;
        case FAN_BOX1_FRONT_2:
            sprintf(path_front, "%s""fan_front_speed_rpm_%d", CPLD_NODE_PATH, 2);
            break;
        case FAN_BOX1_REAR_2:
            sprintf(path_rear, "%s""fan_rear_speed_rpm_%d", CPLD_NODE_PATH, 2);
            break;
        case FAN_BOX2_FRONT_3:
            sprintf(path_front, "%s""fan_front_speed_rpm_%d", CPLD_NODE_PATH, 3);
            break;
        case FAN_BOX2_REAR_3:
            sprintf(path_rear, "%s""fan_rear_speed_rpm_%d", CPLD_NODE_PATH, 3);
            break;
        case FAN_BOX2_FRONT_4:
            sprintf(path_front, "%s""fan_front_speed_rpm_%d", CPLD_NODE_PATH, 4);
            break;
        case FAN_BOX2_REAR_4:
            sprintf(path_rear, "%s""fan_rear_speed_rpm_%d", CPLD_NODE_PATH, 4);
            break;
        default:
            rc = ONLP_STATUS_E_INVALID;
            break;
    }

    switch (fid)
    {
        case FAN_BOX1_FRONT_1:
        case FAN_BOX1_FRONT_2:
        case FAN_BOX2_FRONT_3:
        case FAN_BOX2_FRONT_4:   
            if (onlp_file_read_int(&value, path_front) < 0) {
                AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path_front);
                return ONLP_STATUS_E_INTERNAL;
            }
            info->rpm = value;
            info->percentage = (info->rpm * 100) / MAX_FRONT_FAN_SPEED;
            break;
        case FAN_BOX1_REAR_1:
        case FAN_BOX1_REAR_2:
        case FAN_BOX2_REAR_3:
        case FAN_BOX2_REAR_4:
            if (onlp_file_read_int(&value, path_rear) < 0) {
                AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path_rear);
                return ONLP_STATUS_E_INTERNAL;
            }
            info->rpm = value;
            info->percentage = (info->rpm * 100) / MAX_REAR_FAN_SPEED;
            break;
        default:
            rc = ONLP_STATUS_E_INVALID;
            break;
    }

    return rc;
 }
 
 /*
  * This function will be called prior to all of onlp_fani_* functions.
  */
 int
 onlp_fani_init(void)
 {
    return ONLP_STATUS_OK;
 }
 
 int
 onlp_fani_info_get(onlp_oid_t id, onlp_fan_info_t* info)
 {
     int rc = 0;
     int fid;
     VALIDATE(id);
 
     fid = ONLP_OID_ID_GET(id);
     *info = finfo[fid];
 
     info->status = 0;

     switch (fid)
     {
        case FAN_1_ON_PSU_1:
            rc = _onlp_fani_info_get_fan_on_psu(PSU1_ID, info);
            break;
        case FAN_1_ON_PSU_2:
            rc = _onlp_fani_info_get_fan_on_psu(PSU2_ID, info);
            break;
        case FAN_BOX1_FRONT_1:
        case FAN_BOX1_FRONT_2:
        case FAN_BOX1_REAR_1:
        case FAN_BOX1_REAR_2:
        case FAN_BOX2_FRONT_3:
        case FAN_BOX2_FRONT_4:
        case FAN_BOX2_REAR_3:
        case FAN_BOX2_REAR_4:
            rc =_onlp_fani_info_get_fan(fid, info);						
            break;
        default:
            rc = ONLP_STATUS_E_INVALID;
            break;
     }	
     
     return rc;
 }
 
 /*
  * This function sets the speed of the given fan in RPM.
  *
  * This function will only be called if the fan supprots the RPM_SET
  * capability.
  *
  * It is optional if you have no fans at all with this feature.
  */
 int
 onlp_fani_rpm_set(onlp_oid_t id, int rpm)
 {
     return ONLP_STATUS_E_UNSUPPORTED;
 }
 
 /*
  * This function sets the fan speed of the given OID as a percentage.
  *
  * This will only be called if the OID has the PERCENTAGE_SET
  * capability.
  *
  * It is optional if you have no fans at all with this feature.
  */
 int
 onlp_fani_percentage_set(onlp_oid_t id, int p)
 {
    int  fid;
    char *path = NULL;

    VALIDATE(id);

    fid = ONLP_OID_ID_GET(id);

    /* reject p=0 (p=0, stop fan) */
    if (p == 0){
        return ONLP_STATUS_E_INVALID;
    }

    switch (fid)
    {
        case FAN_BOX1_FRONT_1:
        case FAN_BOX1_FRONT_2:
        case FAN_BOX1_REAR_1:
        case FAN_BOX1_REAR_2:
        case FAN_BOX2_FRONT_3:
        case FAN_BOX2_FRONT_4:
        case FAN_BOX2_REAR_3:
        case FAN_BOX2_REAR_4:
            path = FAN_NODE(fan_duty_cycle_percentage);
            break;
        default:
            return ONLP_STATUS_E_INVALID;
    }

    DEBUG_PRINT("Fan path = (%s)", path);

    if (onlp_file_write_integer(path, p) < 0) {
        AIM_LOG_ERROR("Unable to write data to file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    AIM_LOG_MSG("Set fan_duty_cycle_percentage (%s)=(%d)\r\n", path, p);

    return ONLP_STATUS_OK;
 }
 
 
 /*
  * This function sets the fan speed of the given OID as per
  * the predefined ONLP fan speed modes: off, slow, normal, fast, max.
  *
  * Interpretation of these modes is up to the platform.
  *
  */
 int
 onlp_fani_mode_set(onlp_oid_t id, onlp_fan_mode_t mode)
 {
     return ONLP_STATUS_E_UNSUPPORTED;
 }
 
 /*
  * This function sets the fan direction of the given OID.
  *
  * This function is only relevant if the fan OID supports both direction
  * capabilities.
  *
  * This function is optional unless the functionality is available.
  */
 int
 onlp_fani_dir_set(onlp_oid_t id, onlp_fan_dir_t dir)
 {
     return ONLP_STATUS_E_UNSUPPORTED;
 }
 
 /*
  * Generic fan ioctl. Optional.
  */
 int
 onlp_fani_ioctl(onlp_oid_t id, va_list vargs)
 {
     return ONLP_STATUS_E_UNSUPPORTED;
 }
