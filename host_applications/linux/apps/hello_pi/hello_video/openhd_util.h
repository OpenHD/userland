//
// Created by consti10 on 27.02.23.
//

#ifndef VMCS_HOST_APPS_HOST_APPLICATIONS_LINUX_APPS_HELLO_PI_HELLO_VIDEO_OPENHD_UTIL_H_
#define VMCS_HOST_APPS_HOST_APPLICATIONS_LINUX_APPS_HELLO_PI_HELLO_VIDEO_OPENHD_UTIL_H_

#include <stdio.h>
#include "bcm_host.h"
#include "ilclient.h"

// For users who rotate "QOpenHD"
static int read_rotation_from_file()
{
  const char* file_name="/tmp/video_service_rotation.txt";
  FILE* file = fopen (file_name, "r");
  if(!file)return 0;
  int i = 0;
  if(fscanf(file, "%d", &i)==0){
	i=0;
  };
  fclose(file);
  return i;
}

// "Behind" qt surface
// Rotation controlled by QOpenHD
static void set_display_region( COMPONENT_T *video_render){
  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  memset(&configDisplay, 0, sizeof configDisplay);
  configDisplay.nSize = sizeof configDisplay;
  configDisplay.nVersion.nVersion = OMX_VERSION;
  configDisplay.nPortIndex = 90;

  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
  configDisplay.num = 0;
  configDisplay.layer = -128;
  configDisplay.transform = (OMX_DISPLAYTRANSFORMTYPE)0;
  const int rotation_deg=read_rotation_from_file();
  fprintf(stderr,"Using %d rotation\n",rotation_deg);
  if(rotation_deg==90){
	configDisplay.transform = OMX_DISPLAY_ROT90;
  }
  if(rotation_deg==180){
	configDisplay.transform = OMX_DISPLAY_ROT180;
  }
  if(rotation_deg==270){
	configDisplay.transform = OMX_DISPLAY_ROT270;
  }
  if (OMX_SetConfig(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone){
	fprintf(stderr,"Failed to set omx display config");
  }
}



#endif //VMCS_HOST_APPS_HOST_APPLICATIONS_LINUX_APPS_HELLO_PI_HELLO_VIDEO_OPENHD_UTIL_H_
