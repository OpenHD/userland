/*
Copyright (c) 2012, 2019, Broadcom Europe Ltd
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video deocode demo using OpenMAX IL though the ilcient helper library

#include "time_util.h"
#include "nalu/parse_x20_util.h"

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"
#include "openhd_util.h"

#define OMX_INIT_STRUCTURE(a) \
  memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP

static COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL;
static  TUNNEL_T tunnel[4];
static int status = 0;
static int in_nalu_c=0;
// C21/MPP emits standard Annex-B H.264; keep X20 probing opt-in.
static bool enable_x20_discovery=false;
static bool enable_x21_recovery=true;

// Fixes "hanging" when user changes things like resolution on the fly
static bool changed_once=false;
static bool terminate_and_let_service_restart=false;
static bool allow_repeated_port_settings_changes=false;

static void psc_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
  fprintf(stderr,"got event %p %p %d\n", userdata, comp, data);

  if (comp == video_decode && data == 131) {
	fprintf(stderr,"got event decode port changed, changed_once%s\n",(changed_once ? "Y":"N"));
	if(changed_once){
	  if (allow_repeated_port_settings_changes) {
		fprintf(stderr,"ignoring expected X21 recovery port-settings update\n");
		return;
	  }
	  terminate_and_let_service_restart= true;
	  return;
	}
	changed_once= true;
	if (ilclient_setup_tunnel(tunnel, 0, 0) != 0) {
	  status = -1;
	  fprintf(stderr, "ilclient_setup_tunnel0 failed\n");
	  return;
	}

	ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

	// now setup tunnel to video_render
	if (ilclient_setup_tunnel(tunnel + 1, 0, 1000) != 0) {
	  status = -1;
	  fprintf(stderr, "ilclient_setup_tunnel1 failed\n");
	  return;
	}

	ilclient_change_component_state(video_render, OMX_StateExecuting);
  }
}

void configure_recovery_header(OMX_BUFFERHEADERTYPE *buf, const char* path,
                               const char* label){
    printf("Applying %s recovery seed\n", label);
    FILE *fp = fopen(path, "rb");
    assert(fp);
    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    assert(size>0);
    std::vector<uint8_t> tmp_data(size);
    long read_len=fread(tmp_data.data(), 1, size, fp);
    assert(read_len==size);
    fclose(fp);

    memcpy(buf->pBuffer,tmp_data.data(),read_len);

    buf->nFilledLen = read_len;
    buf->nOffset = 0;

    if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone) {
        fprintf(stderr, "Cannot initialize %s recovery seed\n", label);
    }
}

void util_drop_buffer(OMX_BUFFERHEADERTYPE *buf){
    buf->nFilledLen = 0;
    buf->nOffset = 0;
    if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone) {
        fprintf(stderr, "Cannot give buffer back\n");
    }
}

uint64_t first_frame_ms=0;
bool air_unit_discovery_finished= false;
bool insert_eof= false;
static int video_decode_test(FILE* in) {
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;

  COMPONENT_T *clock = NULL;
  COMPONENT_T *list[5];
  ILCLIENT_T *client;

  memset(list, 0, sizeof(list));
  memset(tunnel, 0, sizeof(tunnel));

  if ((client = ilclient_init()) == NULL) {
	fprintf(stderr, "ilclient_init failed\n");
	return -1;
  }

  if (OMX_Init() != OMX_ErrorNone) {
	ilclient_destroy(client);
	fprintf(stderr, "OMX_init failed\n");
	return -1;
  }

  // create video_decode
  if (ilclient_create_component(client,
								&video_decode,
								(char*)"video_decode",
								(ILCLIENT_CREATE_FLAGS_T)(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0) {
	fprintf(stderr, "video_decode create failed\n");
	return -1;
  }

  list[0] = video_decode;

  // create video_render
  if (ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0) {
	fprintf(stderr, "video_render create failed\n");
	return -1;
  }

  list[1] = video_render;

  if (1) {
	set_display_region(video_render);
  }

  // create clock
  if (ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0) {
	fprintf(stderr, "clock create failed\n");
	return -1;
  }

  list[2] = clock;

  memset(&cstate, 0, sizeof(cstate));
  cstate.nSize = sizeof(cstate);
  cstate.nVersion.nVersion = OMX_VERSION;
  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = 1;

  if (clock != NULL
	  && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone) {
	fprintf(stderr, "OMX set clock state failed\n");
	return -1;
  }

  // create video_scheduler
  if (ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0) {
	fprintf(stderr, "create video_scheduler failed\n");
	return -1;
  }

  list[3] = video_scheduler;

  set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
  set_tunnel(tunnel + 1, video_scheduler, 11, video_render, 90);
  set_tunnel(tunnel + 2, clock, 80, video_scheduler, 12);

  // setup clock tunnel first
  if (ilclient_setup_tunnel(tunnel + 2, 0, 0) != 0) {
	fprintf(stderr, "ilclient_setup_tunnel2 failed\n");
	return -1;
  }

  ilclient_change_component_state(clock, OMX_StateExecuting);
  ilclient_change_component_state(video_decode, OMX_StateIdle);

  memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
  format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
  format.nVersion.nVersion = OMX_VERSION;
  format.nPortIndex = 130;
  format.eCompressionFormat = OMX_VIDEO_CodingAVC;
  format.xFramerate = 90 << 16;
  {
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	OMX_INIT_STRUCTURE(portdef);
	portdef.nPortIndex = 130;
	portdef.nBufferSize = 1024*1024*5;
    portdef.nBufferCountMin=10;
	if(OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamPortDefinition, &portdef) != OMX_ErrorNone){
	  fprintf(stderr, "Cannot set buffer size\n");
	}else{
	  fprintf(stderr, "Set buffer size\n");
	}
  }

  if (OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
	  ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0) {
	OMX_BUFFERHEADERTYPE *buf;
	int first_packet = 1;

	ilclient_change_component_state(video_decode, OMX_StateExecuting);

#ifdef DISPLAY_SET_NOASPECT
	{
		OMX_CONFIG_DISPLAYREGIONTYPE display;
		OMX_ERRORTYPE omx_err;

		OMX_INIT_STRUCTURE(display);
		display.nPortIndex =  90;
		display.set = (OMX_DISPLAYSETTYPE)OMX_DISPLAY_SET_NOASPECT;
		display.noaspect = OMX_TRUE;

		omx_err = OMX_SetParameter(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &display);

		if (omx_err != OMX_ErrorNone)
		{
			fprintf(stderr, "Unable to set aspect: 0x%x\n", omx_err);
			return -1;
		}
	}
#endif

	ilclient_set_port_settings_callback(client, psc_callback, NULL);

	fprintf(stderr, "Initialization done - accepting data Z\n");
	if(insert_eof){
	  fprintf(stderr, "Insert EOF on\n");
	}
	while (status == 0 && (buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL) {
	  //fprintf(stderr, "Read video data\n");
      int data_len = in ? static_cast<int>(fread(buf->pBuffer, 1, buf->nAllocLen, in))
                        : read(STDIN_FILENO, buf->pBuffer, buf->nAllocLen);
	  if (data_len <= 0) break;
      // One pipe read is one decoder submission. A second blocking read here
      // can stall the RTP depayloader while waiting for a large OMX buffer.
	  /*fprintf(stderr,"XBuff size is %d, read %d\n",(int)buf->nAllocLen,data_len);
	  if(check_has_valid_prefix(false,buf->pBuffer,data_len)){
		fprintf(stderr,"Has valid 3 byte prefix\n");
	  }else if(check_has_valid_prefix(true,buf->pBuffer,data_len)) {
		fprintf(stderr, "Has valid 4 byte prefix\n");
	  }else{
		fprintf(stderr, "No valid prefix\n");
	  }
	  if(data_len==6){
		NALU nalu(buf->pBuffer,data_len);
		fprintf(stderr,"Data len==6, type:%d\n",nalu.get_nal_unit_type());
	  }*/

	  if(terminate_and_let_service_restart){
		fprintf(stderr, "Needs restart (probably resolution changed during streaming)\n");
		// Properly terminating hangs for whatever reason - just let the service restart
		exit(0);
		break;
	  }
        if(enable_x20_discovery){
            // X20 auto detection
            if(!air_unit_discovery_finished){
                const int x20_check=check_for_x20(buf->pBuffer,data_len);
                if(x20_check==2){
                    // We have an x20
                    configure_recovery_header(buf, "/usr/local/bin/x20_header.h264",
                                              "X20");
                    air_unit_discovery_finished= true;
                    insert_eof= true;
                    // The recovery seed consumed this OMX input buffer.
                    continue;
                }else if(x20_check==1){
                    // We have no x20 (definitely)
                    air_unit_discovery_finished= true;
                }else{
                    // Unknown if x20 or not
                    // As a bup, we assume no x20 after X seconds
                    if(first_frame_ms==0){
                        first_frame_ms=get_time_ms();
                        // Skip this frame
                        // Don't forget to give buffer back
                        util_drop_buffer(buf);
                        continue;
                    }else{
                        const auto elapsed=get_time_ms()-first_frame_ms;
                        if(elapsed>5*1000){
                            // Assume no x20
                            printf("X20 or not unknown for > 5 seconds\n");
                            air_unit_discovery_finished= true;
                        }else{
                            // Skip this frame
                            util_drop_buffer(buf);
                            continue;
                        }
                    }
                }
            }
        }

        if (enable_x21_recovery && !air_unit_discovery_finished &&
            contains_x21_mpp_sps(buf->pBuffer, data_len)) {
            configure_recovery_header(buf, "/usr/local/bin/x21_header.h264",
                                      "X21/RV1126B");
            air_unit_discovery_finished = true;
            insert_eof = true;
            allow_repeated_port_settings_changes = true;
            continue;
        }

	  //fprintf(stderr, "Got video data %d\n",data_len);
	  /*if(check_has_valid_prefix(false,buf->pBuffer,data_len) || check_has_valid_prefix(true,buf->pBuffer,data_len)){
		in_nalu_c++;
		NALU nalu(buf->pBuffer,data_len);
		fprintf(stderr, "Parsed NALU %d type:%d\n",in_nalu_c,nalu.get_nal_unit_type());
		if(!m_keyframe_finder.check_is_still_same_config_data(nalu)){
		  fprintf(stderr, "Detected changed sps / pps, restart\n");
		  exit(-1);
		}
	  }else{
		fprintf(stderr, "Not a valid NALU %d\n",data_len);
	  }*/

	  buf->nFilledLen = data_len;
	  buf->nOffset = 0;

	  if (first_packet) {
		buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
		first_packet = 0;
	  } else
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

	  // We rely on gstreamer writing an AUD after frame(s) to not create additional latency
	  //buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
	  if(insert_eof){
		buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
	  }

	  //fprintf(stderr, "Begin empty this buffer \n");
	  if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone) {
		status = -1;
		fprintf(stderr, "OMX_EmptyThisBuffer failed\n");
		break;
	  }
	  //fprintf(stderr, "End empty this buffer \n");
	}

	fprintf(stderr, "Broke out of constant decode loop for whatever reason\n");

	// This hangs for whatever reason
	if (buf != NULL) {
	  buf->nOffset=0;
	  buf->nFilledLen = 0;
	  buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;
	  fprintf(stderr, "begin OMX_EmptyThisBuffer (EOS)\n");
	  OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf);
	  fprintf(stderr, "end OMX_EmptyThisBuffer (EOS)\n");
	}

	fprintf(stderr, "ilclient_flush_tunnels\n");
	// need to flush the renderer to allow video_decode to disable its input port
	ilclient_flush_tunnels(tunnel, 0);
	fprintf(stderr, "ilclient_flush_tunnels end\n");
  }

  ilclient_disable_tunnel(tunnel);
  ilclient_disable_tunnel(tunnel + 1);
  ilclient_disable_tunnel(tunnel + 2);
  ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
  ilclient_teardown_tunnels(tunnel);

  ilclient_state_transition(list, OMX_StateIdle);
  ilclient_state_transition(list, OMX_StateLoaded);

  ilclient_cleanup_components(list);

  OMX_Deinit();

  ilclient_destroy(client);
  return status;
}

int main(int argc, char **argv) {
  bcm_host_init();
  const char* filename = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--x20")) enable_x20_discovery = true;
    else if (!strcmp(argv[i], "--eof")) insert_eof = true;
    else if (!filename) filename = argv[i];
  }
  fprintf(stderr, "video_decode_test-begin\n");
  fprintf(stderr,"enable_x20_discovery: %s, enable_x21_recovery: %s\n",
          enable_x20_discovery ? "Y" : "N", enable_x21_recovery ? "Y" : "N");
  FILE *in= nullptr;
  // SysUtils invokes us as `/dev/stdin`; keep that path on `read(2)` rather
  // than stdio `fread`, which can wait for the entire large OMX input buffer.
  if (filename && strcmp(filename, "-") != 0 &&
      strcmp(filename, "/dev/stdin") != 0) {
    if((in = fopen(filename, "rb")) == NULL){
	  return -2;
    }
  }
  int ret=video_decode_test(in);
  if(in){
	fclose(in);
  }
  fprintf(stderr, "video_decode_test-end\n");
  return ret;
}
}
