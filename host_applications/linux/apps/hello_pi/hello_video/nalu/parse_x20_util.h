//
// Created by consti10 on 18.04.24.
//

#ifndef FPVUE_PARSE_X20_UTIL_H
#define FPVUE_PARSE_X20_UTIL_H


#include <cstdint>


#include "NALU.hpp"


static void print_data(const uint8_t* data,int data_len){
    printf("[\n");
    for(int i=0;i<data_len;i++){
        printf("%d,",(int)data[i]);
    }
    printf("]\n");
}

static uint8_t X20_SPS[]={
        0,0,0,1,103,77,0,41,150,84,2,128,45,136,
};
static uint8_t X20_PPS[]={
        0,0,0,1,104,238,49,18,
};

// Return 0: Not yet know
// Return 1: Definitely x20
// Return 2: Definitely not x20
bool has_x20_sps= false;
bool has_x20_pps= false;
static int check_for_x20(const uint8_t* data, int data_len){
    if(data_len<4)return -1;
    if(!NALU::has_valid_prefix(data))return -1;
    NALU tmp(data,data_len);
    const auto type=tmp.get_nal_unit_type();
    printf("Type:%s\n",tmp.get_nal_unit_type_as_string().c_str());
    if(type==NALUnitType::H264::NAL_UNIT_TYPE_SPS){
        //printf("Got SPS\n");
        if(data_len==sizeof(X20_SPS) && memcmp(data,&X20_SPS,data_len)==0){
            printf("X20 SPS\n");
            has_x20_sps= true;
        }else{
            return 2;
        }
    }else if(type==NALUnitType::H264::NAL_UNIT_TYPE_PPS){
        //printf("Got PPS\n");
        //print_data(data,data_len);
        if(data_len==sizeof(X20_PPS) && memcmp(data,&X20_PPS,data_len)==0){
            printf("X20 PPS\n");
            has_x20_pps= true;
        } else{
            return 2;
        }
    }
    if(has_x20_sps && has_x20_pps){
        return 1;
    }
    return 0;
}

// RV1126(B) MPP H.264 defaults to High Profile, Level 4.2 and Annex-B output.
// This lets the Pi decoder select its matching cyclic-intra recovery seed
// without mistaking arbitrary High Profile sources for an X21 stream.
static bool is_x21_mpp_sps(const uint8_t* data, int data_len) {
    return data_len >= 8 && data[0] == 0 && data[1] == 0 &&
           data[2] == 0 && data[3] == 1 && (data[4] & 0x1f) == 7 &&
           data[5] == 0x64 && data[7] == 0x2a;
}

static bool contains_x21_mpp_sps(const uint8_t* data, int data_len) {
    for (int offset = 0; offset + 8 <= data_len; ++offset) {
        if (is_x21_mpp_sps(data + offset, data_len - offset)) return true;
    }
    return false;
}


#endif //FPVUE_PARSE_X20_UTIL_H
