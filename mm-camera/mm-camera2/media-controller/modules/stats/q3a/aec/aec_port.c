/* aec_port.c
 *
 * Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "aec_module.h"
#include "aec_port.h"
#include "q3a_port.h"
#include <pthread.h>
#include "modules.h"
#include "stats_event.h"

#ifdef  DBG_AEC_PORT
#undef  CDBG
#define CDBG CDBG_ERROR
#endif
#define AEC_UNDEF -1
#undef  LOG_TAG
#define LOG_TAG "AEC_PORT"

/** aec_port_malloc_msg:
 *    @msg_type:   TODO
 *    @param_type: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static q3a_thread_aecawb_msg_t* aec_port_malloc_msg(int msg_type,
  int param_type)
{
  q3a_thread_aecawb_msg_t * aec_msg = (q3a_thread_aecawb_msg_t *)
    malloc(sizeof(q3a_thread_aecawb_msg_t));
  if (aec_msg == NULL) {
    return NULL;
  }

  memset(aec_msg, 0 , sizeof(q3a_thread_aecawb_msg_t));
  aec_msg->type = msg_type;
  if (msg_type == MSG_AEC_SET) {
    aec_msg->u.aec_set_parm.type = param_type;
  } else if(msg_type == MSG_AEC_GET) {
    aec_msg->u.aec_get_parm.type = param_type;
  }
  return aec_msg;
}

/** aec_port_send_exif_info_update:
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_send_exif_info_update(mct_port_t *port,
  aec_output_data_t *output)
{
  mct_event_t        event;
  mct_bus_msg_t      bus_msg;
  cam_ae_params_t    aec_info;
  aec_port_private_t *private;
  int                size;
  int                size_user_exif;

  if (!output || !port) {
    ALOGE("%s: input error", __func__);
    return;
  }
  private = (aec_port_private_t *)(port->port_private);
  bus_msg.sessionid = (private->reserved_id >> 16);
  bus_msg.type = MCT_BUS_MSG_AE_INFO;
  bus_msg.msg = (void *)&aec_info;
  size = (int)sizeof(cam_ae_params_t);
  memset(&aec_info, 0, size);
  aec_info.exp_time = output->stats_update.aec_update.exp_time;
  aec_info.iso_value = output->stats_update.aec_update.exif_iso;
  aec_info.flash_needed = output->stats_update.aec_update.flash_needed;
  CDBG("%s: exp_Time:%f, iso:%d, flash:%d", __func__, aec_info.exp_time,
    aec_info.iso_value, aec_info.flash_needed);
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)(&bus_msg);
  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_send_aec_info_to_metadata
 *  update aec info which required by eztuning
 **/

static void aec_port_send_aec_info_to_metadata(
  mct_port_t  *port,
  aec_output_data_t *output)
{
  mct_event_t event;
  mct_bus_msg_t bus_msg;
  aec_ez_tune_t aec_info;
  aec_port_private_t *private;
  int size;
  if (!output || !port) {
    ALOGE("%s: input error", __func__);
    return;
  }
  private = (aec_port_private_t *)(port->port_private);
  bus_msg.sessionid = (private->reserved_id >> 16);
  bus_msg.type = MCT_BUS_MSG_AE_EZTUNING_INFO;
  bus_msg.msg = (void *)&aec_info;
  size = (int)sizeof(aec_ez_tune_t);
  bus_msg.size = size;

  memcpy(&aec_info, &output->eztune, size);

  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)(&bus_msg);
  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_configure_stats
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_configure_stats(aec_output_data_t *output,
  mct_port_t *port)
{
  aec_port_private_t *private = NULL;
  mct_event_t        event;
  stats_config_t     stats_config;
  aec_config_t       config;

  private = (aec_port_private_t *)(port->port_private);

  memset(&stats_config, 0, sizeof(stats_config_t));
  stats_config.aec_config = &(output->config);
  stats_config.stats_mask = (1 << MSM_ISP_STATS_BG)| (1 << MSM_ISP_STATS_BHIST);
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_CONFIG_UPDATE;
  event.u.module_event.module_event_data = (void *)(&stats_config);
  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_send_event:
 *    @port:         TODO
 *    @evt_type:     TODO
 *    @sub_evt_type: TODO
 *    @data:         TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_send_event(mct_port_t *port, int evt_type,
  int sub_evt_type, void *data)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_event_t        event;

  MCT_OBJECT_LOCK(port);
  if (private->aec_update_flag == FALSE ||
    (private->in_zsl_capture == TRUE && private->in_longshot_mode == 0)) {
    CDBG("No AEC update event to send");
    MCT_OBJECT_UNLOCK(port);
    return;
  } else {
    private->aec_update_flag = FALSE;
  }
  MCT_OBJECT_UNLOCK(port);

  /* Pack into an mct_event object */
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = evt_type;
  event.u.module_event.type = sub_evt_type;
  event.u.module_event.module_event_data = data;
  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_save_update:
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_save_update(mct_port_t *port, aec_output_data_t *output)
{
  aec_port_private_t *private = NULL;

  private = (aec_port_private_t *)(port->port_private);
  output->stats_update.flag = STATS_UPDATE_AEC;

  /* HAL uses flash_needed flag to determine if prepare snapshot
   * will be called, hence AEC will turn LED on when the call comes
   * As a result, AEC_EST_NO_LED_DONE is not used (still kept below) */
  if (output->stats_update.aec_update.led_state == Q3A_LED_OFF &&
    private->est_state == AEC_EST_START) {
    if (private->aec_precap_for_af == TRUE) {
      private->est_state = AEC_EST_DONE_FOR_AF;
      private->aec_precap_for_af = FALSE;
    } else {
      private->est_state = AEC_EST_DONE;
    }
    private->aec_precap_start = FALSE;
  } else if (output->stats_update.aec_update.led_state == Q3A_LED_LOW) {
    private->est_state = AEC_EST_START;
    if (private->aec_precap_for_af != TRUE) {
      private->aec_precap_start = TRUE;
    }
  } else if (output->stats_update.aec_update.prep_snap_no_led == TRUE) {
    private->est_state = AEC_EST_NO_LED_DONE;
  } else {
    private->est_state = AEC_EST_OFF;
  }

  output->stats_update.aec_update.est_state = private->est_state;

  if (output->stats_update.aec_update.est_state) {
    CDBG_ERROR("%s:real_gain:%f linecnt:%d exp_idx:%d cur_luma:%d led_est:%d",
      __func__,
      output->stats_update.aec_update.real_gain,
      output->stats_update.aec_update.linecount,
      output->stats_update.aec_update.exp_index,
      output->stats_update.aec_update.cur_luma,
      output->stats_update.aec_update.est_state);
  } else {
    CDBG("%s: real_gain: %f linecnt: %d exp_idx: %d cur_luma:%d led_est: %d",
      __func__,
      output->stats_update.aec_update.real_gain,
      output->stats_update.aec_update.linecount,
      output->stats_update.aec_update.exp_index,
      output->stats_update.aec_update.cur_luma,
      output->stats_update.aec_update.est_state);
  }
  if(output->stats_update.aec_update.frame_id != private->cur_sof_id) {
    /* This will be called when the events happen in the the order
     * STAT1 SOF2 OUTPUT1. It ensures the AEC outputs are always synchronouse
     * with SOFs */
    MCT_OBJECT_LOCK(port);
    private->aec_update_flag = TRUE;
    MCT_OBJECT_UNLOCK(port);
    aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
      MCT_EVENT_MODULE_STATS_AEC_UPDATE, (void *)(&output->stats_update));
    return;
  }
  MCT_OBJECT_LOCK(port);
  memcpy(&(private->aec_update_data), &(output->stats_update),
    sizeof(stats_update_t));
  private->aec_update_flag = TRUE;
  MCT_OBJECT_UNLOCK(port);
}

/** aec_port_callback:
 *    @output: TODO
 *    @p:      TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_callback(aec_output_data_t *output, void *p)
{
  mct_port_t         *port = (mct_port_t *)p;
  aec_port_private_t *private = NULL;
  mct_event_t        event;

  if (!output || !port) {
    CDBG_ERROR("%s: input error", __func__);
    return;
  }
  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return;
  }

  /* if eztune locked, still send meta to bus */
  if ((output->eztune.lock)&& (output->eztune.running)){
    CDBG("%saec_lock =1, send meta\n", __func__);
    aec_port_send_aec_info_to_metadata(port,output);
    return;
  }
  /* populate the stats_upate object to be sent out*/
  if (AEC_UPDATE == output->type ) {
    /*case AEC_FLASH_SETTLE_WAIT*/
    CDBG("%s:%d] send exif update", __func__, __LINE__);
    aec_port_send_exif_info_update(port, output);
    if (output->eztune.running) {
      aec_port_send_aec_info_to_metadata(port,output);
    }
    aec_port_save_update(port, output);
    if (output->need_config) {
      aec_port_configure_stats(output, port);
      output->need_config = 0;
    }
  } else if (AEC_SEND_EVENT == output->type) {
    aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
      MCT_EVENT_MODULE_STATS_AEC_UPDATE,
      (void *)(&private->aec_update_data));
  }
  return;
}

/** aec_port_parse_RDI_stats_AE:
 *    @destLumaBuff: TODO
 *    @rawBuff:      TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_parse_RDI_stats_AE(uint32_t *destLumaBuff,
  void *rawBuff)
{
  uint8_t  *buf = (uint8_t *)rawBuff;
  uint8_t  stat_seq[10];
  uint32_t temp_y = 0;
  int      i = 0;
  int      j = 0;
  int      count = 0;

  if (buf == NULL) {
    CDBG_ERROR("%s: Invalid HDR Stats buf!", __func__);
    return FALSE;
  }

  /* Parse the stats only if camera is started and AF is stopped */

  /* Stats data format:
   * A pair of pixel data (10 bit each) is used for transmission of
   * each sub-block 14 bit data.
   * |y(0,0) U |y(0,0) L | r(0,0)U | r(0,0) L | g(0,0)U | g(0,0)L|
   *  b(0,0)U | b(0,0)L| y(0,1)U | y(0,1)L| r(0,1)U | ...
   * To get 14 bit y-avg we need to combine Upper and Lower pixel:
   * Upper: D13 - D12 - D11 - D10 - D09 - D08 - D07 - D06 - 0 - 1
   * Lower: D05 - D04 - D03 - D02 - D01 - D00 - 0   -   1 - 0 - 1
   * 14-bit final data:
   * D13 - D12 - D11 - D10 - D09 - D8 - D7 - D6 - D5 - D4 - D3 - D2 - D1 -D0
   **/

  /* Each stat_seq holds stats data for each block - we have 16x16 blocks */
  memset(stat_seq, 0, sizeof(stat_seq));
  for (i = 0; i < 16; i++) {
    for (j = 0; j < 16; j++) {
      /* Each pixel is 10 bits. And each stat data has pair of
       * pixel - 20 bits. So for Y/R/G/B we have 80 bits = 10 bytes */
      memcpy((uint8_t *)stat_seq, buf, sizeof(uint8_t) * 10);
      /* Move buf by 80 bits or 10 bytes */
      CDBG("%s: Before moving. buf: %p", __func__, buf);
      buf = buf + 10;
      CDBG("%s: After moving. buf: %p", __func__, buf);
      /* For now we are just concerned with extracting luma value Y */
      temp_y = 0;

      // mask out the higher 4 bits, as only 10bits out of 14bits are
      // valid data
      temp_y = stat_seq[0] & 0x0F;
      temp_y = temp_y << 6;
      temp_y |= (stat_seq[1] & 0x3F);
      CDBG_ERROR("%s: Value obtained for block (%d, %d): %d", __func__,
        i, j, temp_y);

      // shift 2 bits to make 10bits data to 8bits, as AEC algorithm only
      // take 8bits data
      destLumaBuff[count++] = temp_y >> 2;
    }
  }
  return TRUE;
}

/** aec_port_check_identity:
 *    @data1: session identity;
 *    @data2: new identity to compare.
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_check_identity(unsigned int data1, unsigned int data2)
{
  return ((data1 & 0xFFFF0000) == (data2 & 0xFFFF0000)) ? TRUE : FALSE;
}


/** aec_port_proc_downstream_ctrl:
 *    @port:   TODO
 *    @eventl: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_downstream_ctrl(mct_port_t *port,
  mct_event_t *event)
{
  boolean             rc = TRUE;
  aec_port_private_t  *private = (aec_port_private_t *)(port->port_private);
  mct_event_control_t *mod_ctrl = &(event->u.ctrl_event);

  CDBG("%s:  type =%d", __func__, event->u.ctrl_event.type);
  switch (mod_ctrl->type) {
  case MCT_EVENT_CONTROL_PREPARE_SNAPSHOT: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT);
    if (aec_msg != NULL) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  } /* MCT_EVENT_CONTROL_PREPARE_SNAPSHOT */
    break;

  case MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT: {
    /* Unlock AEC update after the ZSL snapshot */
    private->in_zsl_capture = FALSE;

    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_LED_RESET);
    if (aec_msg != NULL) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
  } /* MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT */
    break;

  case MCT_EVENT_CONTROL_STREAMOFF: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_LOCK);
    if (aec_msg != NULL) {
      aec_msg->u.aec_set_parm.u.aec_lock = FALSE;
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
    aec_msg = aec_port_malloc_msg(MSG_AEC_SET, AEC_SET_PARAM_RESET_STREAM_INFO);
    if (aec_msg != NULL ) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
  } /* MCT_EVENT_CONTROL_STREAMOFF */
    break;

  case MCT_EVENT_CONTROL_SET_PARM: {
    /*TO DO: some logic shall be handled by stats and q3a port
      to achieve that, we need to add the function to find the desired sub port;
      however since it is not in place, for now, handle it here
     */
    stats_set_params_type *stat_parm =
      (stats_set_params_type *)mod_ctrl->control_event_data;
    if (stat_parm->param_type == STATS_SET_Q3A_PARAM) {
      q3a_set_params_type  *q3a_param = &(stat_parm->u.q3a_param);
      if (q3a_param->type == Q3A_SET_AEC_PARAM) {
        q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
          q3a_param->u.aec_param.type);
        if (aec_msg != NULL ) {
          aec_msg->u.aec_set_parm = q3a_param->u.aec_param;

          /* for some events we need to peak here */
          switch(q3a_param->u.aec_param.type) {
          case AEC_SET_PARAM_LOCK:
            private->locked_from_hal = q3a_param->u.aec_param.u.aec_lock;
            break;
          case AEC_SET_PARAM_LONGSHOT_MODE:
            private->in_longshot_mode = q3a_param->u.aec_param.u.longshot_mode;
            CDBG("%s longshot_mode: %d", __func__, private->in_longshot_mode);
            break;
          case AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT:
            if(q3a_param->u.aec_param.u.aec_trigger.trigger ==
              AEC_PRECAPTURE_TRIGGER_START) {
              CDBG("%s SET Prepare SNAPSHOT", __func__);
              private->aec_precap_trigger_id =
                q3a_param->u.aec_param.u.aec_trigger.trigger_id;

              if (private->aec_precap_for_af) {
                /* Ignore precapture sequence in the AEC algo since it is
                 * already running for the AF estimation */
                CDBG("%s Ignoring precapture trigger in the algo!!!",
                  __func__);
                q3a_param->u.aec_param.u.aec_trigger.trigger =
                  AEC_PRECAPTURE_TRIGGER_IDLE;
              }
            }
            break;
          case AEC_SET_PARAM_PREP_FOR_SNAPSHOT_NOTIFY:
            if(q3a_param->u.aec_param.u.aec_trigger.trigger ==
              AEC_PRECAPTURE_TRIGGER_START) {
              private->aec_precap_trigger_id =
                q3a_param->u.aec_param.u.aec_trigger.trigger_id;

              CDBG("%s Ignoring precapture trigger (notify) in the algo!!!",
                __func__);
              q3a_param->u.aec_param.u.aec_trigger.trigger =
                AEC_PRECAPTURE_TRIGGER_IDLE;
            }
            break;
          case AEC_SET_PARAM_DO_LED_EST_FOR_AF: {
            private->aec_precap_for_af =
              q3a_param->u.aec_param.u.est_for_af;
          }
            break;
          default:
            break;
          }
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
        }
      } else if (q3a_param->type == Q3A_ALL_SET_PARAM) {
        switch (q3a_param->u.q3a_all_param.type) {
        case Q3A_ALL_SET_EZTUNE_RUNNIG: {
          q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
            AEC_SET_PARAM_EZ_TUNE_RUNNING);
          if (aec_msg != NULL ) {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_EZ_TUNE_RUNNING;
            aec_msg->u.aec_set_parm.u.ez_running =
              q3a_param->u.q3a_all_param.u.ez_runnig;
            rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
          }
        }
          break;
        default: {
        }
          break;
        }
      }
    }
    /* If it's common params shared by many modules */
    else if (stat_parm->param_type == STATS_SET_COMMON_PARAM) {
      stats_common_set_parameter_t *common_param =
        &(stat_parm->u.common_param);
      if (common_param->type == COMMON_SET_PARAM_BESTSHOT ||
        common_param->type == COMMON_SET_PARAM_VIDEO_HDR ||
        common_param->type == COMMON_SET_PARAM_STATS_DEBUG_MASK) {
        q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
          AEC_SET_PARAM_BESTSHOT);
        if (aec_msg != NULL ) {
          switch(common_param->type) {
          case COMMON_SET_PARAM_BESTSHOT: {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_BESTSHOT;
            aec_msg->u.aec_set_parm.u.bestshot_mode =
              common_param->u.bestshot_mode;
          }
            break;

          case COMMON_SET_PARAM_VIDEO_HDR: {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_VIDEO_HDR;
            aec_msg->u.aec_set_parm.u.video_hdr = common_param->u.video_hdr;
            private->video_hdr = common_param->u.video_hdr;
          }
            break;
          case COMMON_SET_PARAM_STATS_DEBUG_MASK: {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_STATS_DEBUG_MASK;
            aec_msg->u.aec_set_parm.u.stats_debug_mask = common_param->u.stats_debug_mask;
          }
            break;
          default: {
          }
            break;
          }
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
        }
      } else if (common_param->type == COMMON_SET_PARAM_ALGO_OPTIMIZATIONS_MASK) {
        /* Save the algo opt mask in port private. It will be used to send the
           mask value along with init chromatix*/
        private->algo_opt_mask = common_param->u.algo_opt_mask;
      }
    }
  }
    break;

  case MCT_EVENT_CONTROL_START_ZSL_SNAPSHOT: {
    /* Lock AEC update only during the ZSL snapshot */
    private->in_zsl_capture = TRUE;

    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
    /*TO do: moving the get exposure handling to here */
  }
    break;

  default: {
  }
    break;
  }
  CDBG("%s: X rc = %d", __func__, rc);
  return rc;
}

/** aec_port_handle_asd_update:
 *    @thread_data: TODO
 *    @mod_evt:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_asd_update(q3a_thread_data_t *thread_data,
  mct_event_module_t *mod_evt)
{
  aec_set_asd_param_t *asd_parm;
  stats_update_t      *stats_event;

  stats_event = (stats_update_t *)(mod_evt->module_event_data);

  CDBG("%s: Handle ASD update!", __func__);
  q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
    AEC_SET_PARAM_ASD_PARM);
  if (aec_msg == NULL) {
    return;
  }
  asd_parm = &(aec_msg->u.aec_set_parm.u.asd_param);
  asd_parm->backlight_detected = stats_event->asd_update.backlight_detected;
  asd_parm->backlight_luma_target_offset =
    stats_event->asd_update.backlight_luma_target_offset;
  asd_parm->snow_or_cloudy_scene_detected =
    stats_event->asd_update.snow_or_cloudy_scene_detected;
  asd_parm->snow_or_cloudy_luma_target_offset =
    stats_event->asd_update.snow_or_cloudy_luma_target_offset;
  asd_parm->landscape_severity = stats_event->asd_update.landscape_severity;
  asd_parm->soft_focus_dgr = stats_event->asd_update.asd_soft_focus_dgr;
  asd_parm->enable = stats_event->asd_update.asd_enable;
  CDBG("%s: backling_detected: %d offset: %d snow_detected: %d offset: %d"
    "landscape_severity: %d soft_focus_dgr: %f", __func__,
    asd_parm->backlight_detected, asd_parm->backlight_luma_target_offset,
    asd_parm->snow_or_cloudy_scene_detected,
    asd_parm->snow_or_cloudy_luma_target_offset,
    asd_parm->landscape_severity, asd_parm->soft_focus_dgr);
  q3a_aecawb_thread_en_q_msg(thread_data, aec_msg);
}

/** aec_port_handle_afd_update:
 *    @thread_data: TODO
 *    @mod_evt:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_afd_update(q3a_thread_data_t *thread_data,
  mct_event_module_t *mod_evt)
{
  aec_set_afd_parm_t *aec_afd_parm;
  stats_update_t     *stats_event;

  stats_event = (stats_update_t *)(mod_evt->module_event_data);
  q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
    AEC_SET_PARAM_AFD_PARM);
  if (aec_msg == NULL) {
    return;
  }
  aec_afd_parm = &(aec_msg->u.aec_set_parm.u.afd_param);
  aec_msg->is_priority = TRUE;
  aec_afd_parm->afd_enable = stats_event->afd_update.afd_enable;
  aec_afd_parm->afd_exec_once = stats_event->afd_update.afd_exec_once;
  aec_afd_parm->afd_monitor = stats_event->afd_update.afd_monitor;

  switch (stats_event->afd_update.afd_atb) {
  case AFD_TBL_OFF: {
    aec_afd_parm->afd_atb =  STATS_PROC_ANTIBANDING_OFF;
  }
    break;

  case AFD_TBL_60HZ: {
    aec_afd_parm->afd_atb = STATS_PROC_ANTIBANDING_60HZ;
  }
    break;

  case AFD_TBL_50HZ: {
    aec_afd_parm->afd_atb = STATS_PROC_ANTIBANDING_50HZ;
  }
    break;

  default: {
    aec_afd_parm->afd_atb =  STATS_PROC_ANTIBANDING_OFF;
  }
    break;
  }
  q3a_aecawb_thread_en_q_msg(thread_data, aec_msg);
}

/** aec_port_proc_get_aec_data:
 *    @port:           TODO
 *    @stats_get_data: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_get_aec_data(mct_port_t *port,
  stats_get_data_t *stats_get_data)
{
  boolean            rc = FALSE;
  aec_output_data_t  output;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);

  if (private->aec_get_data.valid_entries) {
    stats_get_data->aec_get = private->aec_get_data;
    stats_get_data->flag = STATS_UPDATE_AEC;
  } else {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_GET,
      AEC_GET_PARAM_EXPOSURE_PARAMS);

    if (aec_msg) {
      aec_msg->sync_flag = TRUE;
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      if (aec_msg) {
        int i;
        stats_get_data->flag = STATS_UPDATE_AEC;
        stats_get_data->aec_get.valid_entries =
          aec_msg->u.aec_get_parm.u.exp_params.valid_exp_entries;
        for (i=0; i<stats_get_data->aec_get.valid_entries; i++) {
          stats_get_data->aec_get.real_gain[i] =
            aec_msg->u.aec_get_parm.u.exp_params.gain[i];
          stats_get_data->aec_get.linecount[i] =
            aec_msg->u.aec_get_parm.u.exp_params.linecount[i];
        }
        stats_get_data->aec_get.lux_idx =
          aec_msg->u.aec_get_parm.u.exp_params.lux_idx;
        stats_get_data->aec_get.led_off_gain =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_gain;
        stats_get_data->aec_get.led_off_linecount =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_linecount;
        stats_get_data->aec_get.trigger_led =
          aec_msg->u.aec_get_parm.u.exp_params.use_led_estimation;
        stats_get_data->aec_get.exp_time =
          aec_msg->u.aec_get_parm.u.exp_params.exp_time;
        stats_get_data->aec_get.flash_needed =
          aec_msg->u.aec_get_parm.u.flash_needed;
        free(aec_msg);

        private->aec_get_data = stats_get_data->aec_get;
        output.stats_update.aec_update.exp_time =
          stats_get_data->aec_get.exp_time;
        output.stats_update.aec_update.exif_iso =
          stats_get_data->aec_get.real_gain[0] * 100;
        output.stats_update.aec_update.flash_needed =
          stats_get_data->aec_get.flash_needed;
        CDBG_ERROR("ddd exp %f iso %d", stats_get_data->aec_get.exp_time,
          output.stats_update.aec_update.exif_iso);
        aec_port_send_exif_info_update(port, &output);
      }
    } else {
      CDBG_ERROR("%s:%d Not enough memory", __func__, __LINE__);
    }
  }

  return rc;
}

/** aec_port_proc_downstream_event
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_buf_divert(mct_port_t *port, mct_event_t *event)
{
  boolean              rc = TRUE;
  mct_event_t          stats_event;
  isp_buf_divert_t     stats_module_ack_key =
    *((isp_buf_divert_t *)event->u.module_event.module_event_data);
  isp_buf_divert_ack_t isp_divert_ack;

  isp_divert_ack.buf_idx = stats_module_ack_key.buffer.index;
  isp_divert_ack.channel_id = stats_module_ack_key.channel_id;
  isp_divert_ack.identity = stats_module_ack_key.identity;
  isp_divert_ack.is_buf_dirty = TRUE;

  CDBG("%s:%d E", __func__, __LINE__);

  stats_event.direction = MCT_EVENT_UPSTREAM;
  stats_event.identity = event->identity;
  stats_event.type = MCT_EVENT_MODULE_EVENT;
  stats_event.u.module_event.type = MCT_EVENT_MODULE_BUF_DIVERT_ACK;
  stats_event.u.module_event.module_event_data =
    (void *)(&(isp_divert_ack));
  CDBG("%s: Send divert event to ISP identity %d\n", __func__,
    stats_event.identity);
  mct_port_send_event_to_peer(port, &stats_event);
}

/** aec_port_proc_downstream_event:
 *    port:  TODO
 *    event: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_downstream_event(mct_port_t *port,
  mct_event_t *event)
{
  boolean            rc = TRUE;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_event_module_t *mod_evt = &(event->u.module_event);

  switch (mod_evt->type) {
  case MCT_EVENT_MODULE_STATS_GET_THREAD_OBJECT: {
    q3a_thread_aecawb_data_t *data =
      (q3a_thread_aecawb_data_t *)(mod_evt->module_event_data);

    private->thread_data = data->thread_data;

    data->aec_port = port;
    data->aec_cb   = aec_port_callback;
    data->aec_obj  = &(private->aec_object);
    rc = TRUE;
  } /* case MCT_EVENT_MODULE_STATS_GET_THREAD_OBJECT */
    break;

  case MCT_EVENT_MODULE_SOF_NOTIFY:{
    mct_bus_msg_isp_sof_t *sof_event;

    sof_event = (mct_bus_msg_isp_sof_t *)(mod_evt->module_event_data);
    private->cur_sof_id = sof_event->frame_id;
    q3a_thread_aecawb_msg_t *aec_msg =
      aec_port_malloc_msg(MSG_AEC_SEND_EVENT, AEC_UNDEF);
    if (aec_msg == NULL) {
      break;
    }
    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
  } /*MCT_EVENT_MODULE_SOF_NOTIFY*/
    break;

  case MCT_EVENT_MODULE_STATS_DATA: {
    mct_event_stats_isp_t *stats_event ;

    stats_event = (mct_event_stats_isp_t *)(mod_evt->module_event_data);
    if (private->stream_type == CAM_STREAM_TYPE_SNAPSHOT  ||
             private->stream_type == CAM_STREAM_TYPE_RAW) {
      break;
    }
    if (stats_event) {
      if(!((stats_event->stats_mask & (1 << MSM_ISP_STATS_BHIST)) ||
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_BG)) ||
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_AEC)) ||
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_IHIST)))) {
        break;
      }
      /* We don't want to process ISP stats if video HDR mode is ON */
      if(((stats_event->stats_mask & (1 << MSM_ISP_STATS_BG)) ||
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_AEC))) &&
        (private->video_hdr != 0)) {
        break;
      }
      q3a_thread_aecawb_msg_t *aec_msg =
        aec_port_malloc_msg(AEC_UNDEF, AEC_UNDEF);
      if (aec_msg == NULL) {
        break;
      }
      stats_t *aec_stats = (stats_t *)calloc(1, sizeof(stats_t));

      if (aec_stats == NULL) {
        free(aec_msg);
        break;
      }
      aec_msg->u.stats = aec_stats;
      aec_stats->stats_type_mask = 0;
      aec_stats->frame_id = stats_event->frame_id;
      rc = FALSE;
      CDBG("%s: event stats_mask =0x%x,", __func__, stats_event->stats_mask);
      if (stats_event->stats_mask & (1 << MSM_ISP_STATS_AEC)) {
        aec_msg->type = MSG_AEC_STATS;
        aec_stats->stats_type_mask |= STATS_AEC;
        memcpy(&aec_stats->yuv_stats.q3a_aec_stats,
          stats_event->stats_data[MSM_ISP_STATS_AEC].stats_buf,
          sizeof(q3a_aec_stats_t));
        rc = TRUE;
      }
      if (stats_event->stats_mask & (1 << MSM_ISP_STATS_BG)) {
        aec_stats->stats_type_mask |= STATS_BG;
        aec_msg->type = MSG_BG_AEC_STATS;
        memcpy(&aec_stats->bayer_stats.q3a_bg_stats,
          stats_event->stats_data[MSM_ISP_STATS_BG].stats_buf,
          sizeof(q3a_bg_stats_t));
        rc = TRUE;
      }
      /* Ensure BG or AEC stats are preset to propagate to AEC algorithm.
      If it is missing then ignore the composite stats */
      if (rc && stats_event->stats_mask & (1 << MSM_ISP_STATS_BHIST)) {
        memcpy(&aec_stats->bayer_stats.q3a_bhist_stats,
          stats_event->stats_data[MSM_ISP_STATS_BHIST].stats_buf,
          sizeof(q3a_bhist_stats_t));
      }
      if (rc && stats_event->stats_mask & (1 << MSM_ISP_STATS_IHIST)) {
        memcpy(&aec_stats->yuv_stats.q3a_ihist_stats,
          stats_event->stats_data[MSM_ISP_STATS_IHIST].stats_buf,
          sizeof(q3a_ihist_stats_t));
      }
      if(!rc) {
        free(aec_stats);
        free(aec_msg);
        break;
      }
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      /*If the aecawb thread is inactive, it will not enqueue our
       * message and instead will free it. Then we need to manually
       * free the payload */
      if (rc == FALSE) {
        free(aec_stats);
      }
    }
  } /* case MCT_EVENT_MODULE_STATS_DATA */
    break;

  case MCT_EVENT_MODULE_BUF_DIVERT: {
    isp_buf_divert_t        *hdr_stats_buff =
      (isp_buf_divert_t *)mod_evt->module_event_data;
    q3a_thread_aecawb_msg_t *aec_msg =
      (q3a_thread_aecawb_msg_t *)malloc(sizeof(q3a_thread_aecawb_msg_t));
    stats_module_ack_key_t  key;

    key.identity = event->identity;
    key.buf_idx = hdr_stats_buff->buffer.index;
    key.channel_id = hdr_stats_buff->channel_id;

    CDBG("aec_msg=%p, HDR stats", aec_msg);

    if (aec_msg == NULL ) {
      CDBG_ERROR("%s:%d Not enough memory", __func__, __LINE__);
      break;
    }
    memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));

    stats_t *aec_stats = (stats_t *)calloc(1, sizeof(stats_t));
    if (aec_stats == NULL) {
      CDBG_ERROR("%s:%d Not enough memory", __func__, __LINE__);
      free(aec_msg);
      break;
    }

    aec_msg->u.stats = aec_stats;
    aec_msg->type = MSG_AEC_STATS_HDR;
    aec_stats->stats_type_mask |= STATS_HDR_VID;
    aec_stats->yuv_stats.q3a_aec_stats.ae_region_h_num = 16;
    aec_stats->yuv_stats.q3a_aec_stats.ae_region_v_num = 16;
    aec_stats->frame_id = hdr_stats_buff->buffer.sequence;
    aec_port_parse_RDI_stats_AE(aec_stats->yuv_stats.q3a_aec_stats.SY,
      hdr_stats_buff->vaddr);

    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);

    aec_port_handle_buf_divert(port, event);
  }
    break;

  case MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX:
  case MCT_EVENT_MODULE_SET_CHROMATIX_PTR: {
    modulesChromatix_t *mod_chrom =
      (modulesChromatix_t *)mod_evt->module_event_data;
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_INIT_CHROMATIX_SENSOR);

    CDBG("aec_msg=%p,SET_CHROMATIX", aec_msg);

    if (aec_msg != NULL ) {
      /*To Do: for now hard-code the stats type and op_mode for now.*/
      aec_msg->u.aec_set_parm.u.init_param.stats_type = STATS_BE;

      switch (private->stream_type) {
      case CAM_STREAM_TYPE_VIDEO: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
      }
        break;

      case CAM_STREAM_TYPE_PREVIEW: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_PREVIEW;
      }
        break;

      case CAM_STREAM_TYPE_RAW:
      case CAM_STREAM_TYPE_SNAPSHOT: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
           AEC_OPERATION_MODE_SNAPSHOT;
      }
        break;

      default: {
      }
        break;
      } /* switch (private->stream_type) */

      CDBG("%s:stream_type=%d op_mode=%d", __func__,
        private->stream_type, aec_msg->u.aec_set_parm.u.init_param.op_mode);

      aec_msg->u.aec_set_parm.u.init_param.chromatix = mod_chrom->chromatixPtr;
      aec_msg->u.aec_set_parm.u.init_param.comm_chromatix =
        mod_chrom->chromatixComPtr;

      aec_msg->u.aec_set_parm.u.init_param.aec_tuning_params.aec_subsampling_factor =
        ((private->algo_opt_mask & STATS_MASK_AEC) ?
        (AEC_SUBSAMPLE) : (MIN_AEC_SUBSAMPLE));

      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    } /* if (aec_msg != NULL ) */
  } /* case MCT_EVENT_MODULE_SET_CHROMATIX_PTR */
    break;

  case MCT_EVENT_MODULE_PREVIEW_STREAM_ID: {
    aec_set_parameter_init_t *init_param;
    q3a_thread_aecawb_msg_t  *dim_msg;
    mct_stream_info_t  *stream_info =
      (mct_stream_info_t *)(mod_evt->module_event_data);

    CDBG("Preview stream-id event: stream_type: %d width: %d height: %d",
      stream_info->stream_type, stream_info->dim.width, stream_info->dim.height);

    private->preview_width = stream_info->dim.width;
    private->preview_height = stream_info->dim.height;
  }
    break;

  case MCT_EVENT_MODULE_SET_STREAM_CONFIG: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_INIT_SENSOR_INFO);
    sensor_out_info_t       *sensor_info =
      (sensor_out_info_t *)(mod_evt->module_event_data);

    /* TBG to change to sensor */
    float fps, max_fps;

    if (aec_msg != NULL ) {
      fps = sensor_info->max_fps * 0x00000100;

      /*max fps supported by sensor*/
      max_fps = (float)sensor_info->vt_pixel_clk * 0x00000100 /
        (float)(sensor_info->ll_pck * sensor_info->fl_lines);
      switch (private->stream_type) {
      case CAM_STREAM_TYPE_VIDEO: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.video_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.video_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.max_preview_fps = max_fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_fps = fps;
      }
        break;

      case CAM_STREAM_TYPE_PREVIEW: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_PREVIEW;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.max_preview_fps = max_fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_fps = fps;
      }
        break;

      case CAM_STREAM_TYPE_RAW:
      case CAM_STREAM_TYPE_SNAPSHOT: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_SNAPSHOT;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snap_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snap_max_line_cnt =
          sensor_info->max_linecount;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snapshot_fps = fps;
      }
        break;

      default: {
      }
        break;
      } /* switch (private->stream_type) */

      aec_msg->u.aec_set_parm.u.init_param.sensor_info.current_fps = fps;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.pixel_clock =
        sensor_info->vt_pixel_clk;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.pixel_clock_per_line =
        sensor_info->ll_pck;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.sensor_res_height =
        sensor_info->request_crop.last_line -
        sensor_info->request_crop.first_line + 1;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.sensor_res_width =
        sensor_info->request_crop.last_pixel -
        sensor_info->request_crop.first_pixel + 1;
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);

      /* Also send the stream dimensions for preview */
      if ((private->stream_type == CAM_STREAM_TYPE_PREVIEW) ||
          (private->stream_type == CAM_STREAM_TYPE_VIDEO)) {
        aec_set_parameter_t      *params;
        aec_set_parameter_init_t *init_param;
        q3a_thread_aecawb_msg_t  *dim_msg;

        dim_msg = aec_port_malloc_msg(MSG_AEC_SET, AEC_SET_PARAM_UI_FRAME_DIM);
        if (!dim_msg) {
          CDBG_ERROR("malloc failed for dim_msg");
          break;
        }
        init_param = &(dim_msg->u.aec_set_parm.u.init_param);
        init_param->frame_dim.width = private->preview_width;
        init_param->frame_dim.height = private->preview_height;
        CDBG("enqueue msg update ui width %d and height %d",
          init_param->frame_dim.width, init_param->frame_dim.height);

        rc = q3a_aecawb_thread_en_q_msg(private->thread_data, dim_msg);
      }
    }
  } /* MCT_EVENT_MODULE_SET_STREAM_CONFIG*/
    break;

  case MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE: {
    stats_get_data_t *stats_get_data =
      (stats_get_data_t *)mod_evt->module_event_data;
    if (!stats_get_data) {
      CDBG_ERROR("%s:%d failed\n", __func__, __LINE__);
      break;
    }

    aec_port_proc_get_aec_data(port, stats_get_data);
  } /* MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE X*/
    break;

  case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
    mct_stream_info_t *stream_info =
      (mct_stream_info_t *)(event->u.module_event.module_event_data);
    if(!stream_info) {
      CDBG_ERROR("%s:%d, failed\n", __func__, __LINE__);
      break;
    }
    private->vfe_out_width  = stream_info->dim.width;
    private->vfe_out_height = stream_info->dim.height;
  }
    break;

  case MCT_EVENT_MODULE_STREAM_CROP: {
    mct_bus_msg_stream_crop_t *stream_crop =
      (mct_bus_msg_stream_crop_t *)mod_evt->module_event_data;

    if (!stream_crop) {
      CDBG_ERROR("%s:%d failed\n", __func__, __LINE__);
      break;
    }
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_CROP_INFO);
    if (aec_msg != NULL ) {
      aec_msg->u.aec_set_parm.u.stream_crop.pp_x = stream_crop->x;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_y = stream_crop->y;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_crop_out_x =
        stream_crop->crop_out_x;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_crop_out_y =
        stream_crop->crop_out_y;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_x = stream_crop->x_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_y = stream_crop->y_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_width =
        stream_crop->width_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_height =
        stream_crop->height_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_out_width =
        private->vfe_out_width;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_out_height =
        private->vfe_out_height;
      CDBG("Crop Event from ISP received. PP (%d %d %d %d)", stream_crop->x,
        stream_crop->y, stream_crop->crop_out_x, stream_crop->crop_out_y);
      CDBG("vfe map: (%d %d %d %d) vfe_out: (%d %d)", stream_crop->x_map,
        stream_crop->y_map, stream_crop->width_map, stream_crop->height_map,
        private->vfe_out_width, private->vfe_out_height);
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;

  case MCT_EVENT_MODULE_FACE_INFO: {
    mct_face_info_t *face_info = (mct_face_info_t *)mod_evt->module_event_data;
    if (!face_info) {
      CDBG_ERROR("%s:%d failed\n", __func__, __LINE__);
      break;
    }
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_FD_ROI);
    if (aec_msg != NULL) {
      uint8_t idx = 0;

      /* Copy original face coordinates, do the transform in aec_set.c */
      aec_msg->u.aec_set_parm.u.fd_roi.type = ROI_TYPE_GENERAL;
      aec_msg->u.aec_set_parm.u.fd_roi.num_roi = face_info->face_count;
      for (idx = 0; idx < aec_msg->u.aec_set_parm.u.fd_roi.num_roi; idx++) {
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].x =
          face_info->orig_faces[idx].roi.left;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].y =
          face_info->orig_faces[idx].roi.top;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].dx =
          face_info->orig_faces[idx].roi.width;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].dy =
          face_info->orig_faces[idx].roi.height;
      }
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;

  case MCT_EVENT_MODULE_STATS_GET_DATA: {
    stats_get_data_t *stats_get_data =
      (stats_get_data_t *)mod_evt->module_event_data;
    if (!stats_get_data) {
      CDBG_ERROR("%s:%d failed\n", __func__, __LINE__);
      break;
    }

    aec_port_proc_get_aec_data(port, stats_get_data);
  }
    break;

  case MCT_EVENT_MODULE_STATS_AFD_UPDATE: {
    aec_port_handle_afd_update(private->thread_data, mod_evt);
  }
    break;

  case MCT_EVENT_MODULE_STATS_AWB_UPDATE: {
    stats_update_t *stats_update =
      (stats_update_t *)(mod_evt->module_event_data);
    if (stats_update->flag != STATS_UPDATE_AWB) {
      break;
    }
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_AWB_PARM);
    if (aec_msg != NULL ) {
      aec_set_awb_parm_t *awb_param;
      awb_param = &(aec_msg->u.aec_set_parm.u.awb_param);
      awb_param->r_gain = stats_update->awb_update.gain.r_gain;
      awb_param->g_gain = stats_update->awb_update.gain.g_gain;
      awb_param->b_gain = stats_update->awb_update.gain.b_gain;
      q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    } /* if (aec_msg != NULL ) */
  } /* MCT_EVENT_MODULE_STATS_AWB_UPDATE*/
    break;

  case MCT_EVENT_MODULE_STATS_ASD_UPDATE: {
    aec_port_handle_asd_update(private->thread_data, mod_evt);
  }
    break;
  case MCT_EVENT_MODULE_STATS_GYRO_STATS: {
    aec_algo_gyro_info_t *gyro_info = NULL;
    mct_event_gyro_stats_t *gyro_update =
      (mct_event_gyro_stats_t *)mod_evt->module_event_data;
    if (gyro_update == NULL) break;

    int i = 0;
    q3a_thread_aecawb_msg_t *aec_msg = (q3a_thread_aecawb_msg_t *)
      malloc(sizeof(q3a_thread_aecawb_msg_t));
    if (aec_msg != NULL) {
      memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
      aec_msg->type = MSG_AEC_SET;
      aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_GYRO_INFO;
      /* Copy gyro data now */
      gyro_info = &(aec_msg->u.aec_set_parm.u.gyro_info);
      gyro_info->q16_ready = TRUE;
      gyro_info->float_ready = TRUE;

      for (i = 0; i < GYRO_DIM; i++) {
        gyro_info->q16[i] = (long) gyro_update->q16_angle[i];
        gyro_info->flt[i] = (float) gyro_update->q16_angle[i] / GYRO_Q16;
        CDBG("%s: i: %d q16: %ld flt: %f", __func__, i,
          gyro_info->q16[i], gyro_info->flt[i]);
      }
      q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;
  case MCT_EVENT_MODULE_MODE_CHANGE: {
    /* Stream mode has changed */
    private->stream_type =
      ((stats_mode_change_event_data*)
      (event->u.module_event.module_event_data))->stream_type;
    private->reserved_id =
      ((stats_mode_change_event_data*)
      (event->u.module_event.module_event_data))->reserved_id;
  }
    break;

  case MCT_EVENT_MODULE_LED_STATE_TIMEOUT: {
    CDBG_ERROR("%s: Received LED state timeout. Reset LED state!", __func__);
    q3a_thread_aecawb_msg_t *reset_led_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_RESET_LED_EST);

    if (!reset_led_msg) {
      CDBG_ERROR("malloc failed for dim_msg");
      break;
    }
    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, reset_led_msg);
  }
    break;


  default: {
  }
    break;
  } /* switch (mod_evt->type) */
  return rc;
}
/** aec_port_event:
 *    @port:  TODO
 *    @event: TODO
 *
 * aec sink module's event processing function. Received events could be:
 * AEC/AWB/AF Bayer stats;
 * Gyro sensor stat;
 * Information request event from other module(s);
 * Informatin update event from other module(s);
 * It ONLY takes MCT_EVENT_DOWNSTREAM event.
 *
 * Return TRUE if the event is processed successfully.
 **/
static boolean aec_port_event(mct_port_t *port, mct_event_t *event)
{
  boolean rc = FALSE;
  aec_port_private_t *private;

  /* sanity check */
  if (!port || !event) {
    return FALSE;
  }

  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return FALSE;
  }

  /* sanity check: ensure event is meant for port with same identity*/
  if (!aec_port_check_identity(private->reserved_id, event->identity)) {
    return FALSE;
  }

  CDBG("%s: event dir=%d, type=%d", __func__, MCT_EVENT_DIRECTION(event),
    event->type);
  switch (MCT_EVENT_DIRECTION(event)) {
  case MCT_EVENT_DOWNSTREAM: {
    CDBG("%s: down event ", __func__);
    switch (event->type) {
    case MCT_EVENT_MODULE_EVENT: {
      CDBG("%s: mod event type =%d", __func__, event->u.module_event.type);
      rc= aec_port_proc_downstream_event(port, event);
    } /* case MCT_EVENT_MODULE_EVENT */
      break;

    case MCT_EVENT_CONTROL_CMD: {
      CDBG("%s: ctrl event type =%d", __func__, event->u.ctrl_event.type);
      rc = aec_port_proc_downstream_ctrl(port,event);
    }
      break;

    default: {
    }
      break;
    }
  } /* case MCT_EVENT_TYPE_DOWNSTREAM */
    break;

  case MCT_EVENT_UPSTREAM: {
    CDBG("%s: Send upstream event", __func__);
    mct_port_t *peer = MCT_PORT_PEER(port);
    MCT_PORT_EVENT_FUNC(peer)(peer, event);
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

  return rc;
}

/** aec_port_ext_link:
 *    @identity: session id + stream id
 *    @port:  aec module's sink port
 *    @peer:  q3a module's sink port
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_ext_link(unsigned int identity,
  mct_port_t *port, mct_port_t *peer)
{
  boolean            rc = FALSE;
  aec_port_private_t *private;
  CDBG("%s:%d", __func__, __LINE__);

  /* aec sink port's external peer is always q3a module's sink port */
  if (!port || !peer ||
    strcmp(MCT_OBJECT_NAME(port), "aec_sink") ||
    strcmp(MCT_OBJECT_NAME(peer), "q3a_sink")) {
    CDBG_ERROR("%s: Invalid parameters!", __func__);
    return FALSE;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    CDBG_ERROR("%s: Null Private port!", __func__);
    return FALSE;
  }

  MCT_OBJECT_LOCK(port);
  switch (private->state) {
  case AEC_PORT_STATE_RESERVED:
    /* Fall through, no break */
  case AEC_PORT_STATE_UNLINKED:
    /* Fall through, no break */
  case AEC_PORT_STATE_LINKED: {
    if (!aec_port_check_identity(private->reserved_id, identity)) {
      break;
    }
  }
  /* Fall through, no break */
  case AEC_PORT_STATE_CREATED: {
    rc = TRUE;
  }
    break;

  default: {
  }
    break;
  }

  if (rc == TRUE) {
    private->state = AEC_PORT_STATE_LINKED;
    MCT_PORT_PEER(port) = peer;
    MCT_OBJECT_REFCOUNT(port) += 1;
  }
  MCT_OBJECT_UNLOCK(port);

  CDBG("%s:%d X rc=%d", __func__, __LINE__, rc);
  return rc;
}

/** aec_port_ext_unlink
 *    @identity: TODO
 *    @port:     TODO
 *    @peer:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_ext_unlink(unsigned int identity, mct_port_t *port,
  mct_port_t *peer)
{
  aec_port_private_t *private;

  if (!port || !peer || MCT_PORT_PEER(port) != peer) {
    return;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    return;
  }

  MCT_OBJECT_LOCK(port);
  if (private->state == AEC_PORT_STATE_LINKED &&
    aec_port_check_identity(private->reserved_id, identity)) {
    MCT_OBJECT_REFCOUNT(port) -= 1;
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state = AEC_PORT_STATE_UNLINKED;
    }
  }
  MCT_OBJECT_UNLOCK(port);

  return;
}

/** aec_port_set_caps:
 *    @port: TODO
 *    @caps: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_set_caps(mct_port_t *port, mct_port_caps_t *caps)
{
  if (strcmp(MCT_PORT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  port->caps = *caps;
  return TRUE;
}

/** aec_port_check_caps_reserve:
 *    @port:        TODO
 *    @caps:        TODO
 *    @stream_info: TODO
 *
 *  AEC sink port can ONLY be re-used by ONE session. If this port
 *  has been in use, AEC module has to add an extra port to support
 *  any new session(via module_aec_request_new_port).
 *
 * TODO Return
 **/
boolean aec_port_check_caps_reserve(mct_port_t *port, void *caps,
  mct_stream_info_t *stream_info)
{
  boolean            rc = FALSE;
  mct_port_caps_t    *port_caps;
  aec_port_private_t *private;

  CDBG("%s: E", __func__);
  MCT_OBJECT_LOCK(port);
  if (!port || !caps || !stream_info ||
    strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    rc = FALSE;
    goto reserve_done;
  }

  port_caps = (mct_port_caps_t *)caps;
  if (port_caps->port_caps_type != MCT_PORT_CAPS_STATS) {
    rc = FALSE;
    goto reserve_done;
  }

  private = (aec_port_private_t *)port->port_private;
  switch (private->state) {
  case AEC_PORT_STATE_LINKED: {
    if (aec_port_check_identity(private->reserved_id, stream_info->identity)) {
      rc = TRUE;
    }
  }
    break;

  case AEC_PORT_STATE_CREATED:
  case AEC_PORT_STATE_UNRESERVED: {
    private->reserved_id = stream_info->identity;
    private->stream_type = stream_info->stream_type;
    private->stream_info = *stream_info;
    private->state       = AEC_PORT_STATE_RESERVED;
    rc = TRUE;
  }
    break;

  case AEC_PORT_STATE_RESERVED: {
    if (aec_port_check_identity(private->reserved_id, stream_info->identity)) {
      rc = TRUE;
    }
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

reserve_done:
  MCT_OBJECT_UNLOCK(port);
  return rc;
}

/** aec_port_check_caps_unreserve:
 *    port:     TODO
 *    identity: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_check_caps_unreserve(mct_port_t *port,
  unsigned int identity)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    return FALSE;
  }

  MCT_OBJECT_LOCK(port);
  if ((private->state == AEC_PORT_STATE_UNLINKED ||
    private->state == AEC_PORT_STATE_LINKED ||
    private->state == AEC_PORT_STATE_RESERVED) &&
    aec_port_check_identity(private->reserved_id, identity)) {
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state       = AEC_PORT_STATE_UNRESERVED;
      private->reserved_id = (private->reserved_id & 0xFFFF0000);
    }
  }
  MCT_OBJECT_UNLOCK(port);

  return TRUE;
}

/** aec_port_find_identity:
 *    @port:     TODO
 *    @identity: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
boolean aec_port_find_identity(mct_port_t *port, unsigned int identity)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = port->port_private;

  if (private) {
    return ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000) ?
      TRUE : FALSE);
  }
  return FALSE;
}

/** aec_port_deinit
 *    @port: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
void aec_port_deinit(mct_port_t *port)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return;
  }

  private = port->port_private;
  if (!private) {
    return;
  }

  AEC_DESTROY_LOCK(&private->aec_object);
  private->aec_object.deinit(private->aec_object.aec);
  free(private);
  private = NULL;
}

/** aec_port_init:
 *    @port:      aec's sink port to be initialized
 *    @sessionid: TODO
 *
 *  aec port initialization entry point. Becase AEC module/port is
 *  pure software object, defer aec_port_init when session starts.
 *
 * TODO Return
 **/
boolean aec_port_init(mct_port_t *port, unsigned int *sessionid)
{
  boolean            rc = TRUE;
  mct_port_caps_t    caps;
  unsigned int       *session;
  mct_list_t         *list;
  aec_port_private_t *private;
  unsigned int       session_id =(((*sessionid) >> 16) & 0x00ff);

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = (void *)malloc(sizeof(aec_port_private_t));
  if (!private) {
    return FALSE;
  }
  memset(private, 0, sizeof(aec_port_private_t));
  /* initialize AEC object */
  AEC_INITIALIZE_LOCK(&private->aec_object);
  aec_load_function(&private->aec_object, session_id);

  private->aec_object.aec = private->aec_object.init();
  CDBG("%s: %d: aec=%p", __func__, __LINE__, private->aec_object.aec);

  if (private->aec_object.aec == NULL) {
    free(private);
    private = NULL;
    return FALSE;
  }

  private->reserved_id = *sessionid;
  private->state       = AEC_PORT_STATE_CREATED;

  port->port_private   = private;
  port->direction      = MCT_PORT_SINK;
  caps.port_caps_type  = MCT_PORT_CAPS_STATS;
  caps.u.stats.flag    = (MCT_PORT_CAP_STATS_Q3A | MCT_PORT_CAP_STATS_CS_RS);

  /* this is sink port of aec module */
  mct_port_set_event_func(port, aec_port_event);
  mct_port_set_ext_link_func(port, aec_port_ext_link);
  mct_port_set_unlink_func(port, aec_port_ext_unlink);
  mct_port_set_set_caps_func(port, aec_port_set_caps);
  mct_port_set_check_caps_reserve_func(port, aec_port_check_caps_reserve);
  mct_port_set_check_caps_unreserve_func(port, aec_port_check_caps_unreserve);

  if (port->set_caps) {
    port->set_caps(port, &caps);
  }

  return TRUE;
}
