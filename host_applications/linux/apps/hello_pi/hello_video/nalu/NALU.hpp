//
// Created by Constantin on 2/6/2019.
//

#ifndef LIVE_VIDEO_10MS_ANDROID_NALU_H
#define LIVE_VIDEO_10MS_ANDROID_NALU_H

//https://github.com/Dash-Industry-Forum/Conformance-and-reference-source/blob/master/conformance/TSValidator/h264bitstream/h264_stream.h

#include <cstring>
#include <string>
#include <chrono>
#include <sstream>
#include <array>
#include <vector>
#include <variant>
#include <optional>
#include <assert.h>
#include <memory>

#include "NALUnitType.hpp"

/**
 * A NALU either contains H264 data (default) or H265 data
 * NOTE: Only when copy constructing a NALU it owns the data, else it only holds a data pointer (that might get overwritten by the parser if you hold onto a NALU)
 * Also, H264 and H265 is slightly different
 * The constructor of the NALU does some really basic validation - make sure the parser never produces a NALU where this validation would fail
 */
class NALU{
public:
    // test video white iceland: Max 1024*117. Video might not be decodable if its NALU buffers size exceed the limit
    // But a buffer size of 1MB accounts for 60fps video of up to 60MB/s or 480 Mbit/s. That should be plenty !
    static constexpr const auto NALU_MAXLEN=1024*1024;
    // Application should re-use NALU_BUFFER to avoid memory allocations
    using NALU_BUFFER=std::array<uint8_t,NALU_MAXLEN>;
    // Copy constructor allocates new buffer for data (heavy)
    NALU(const NALU& nalu):
    ownedData(std::vector<uint8_t>(nalu.getData(),nalu.getData()+nalu.getSize())),
    m_data(ownedData->data()),m_data_len(nalu.getSize()),IS_H265_PACKET(nalu.IS_H265_PACKET),creationTime(nalu.creationTime){
        //MLOGD<<"NALU copy constructor";
        m_nalu_prefix_size=get_nalu_prefix_size();
    }
    // Default constructor does not allocate a new buffer,only stores some pointer (light)
    NALU(const NALU_BUFFER& data1,const size_t data_length,const bool IS_H265_PACKET1=false,const std::chrono::steady_clock::time_point creationTime=std::chrono::steady_clock::now()):
            m_data(data1.data()),m_data_len(data_length),IS_H265_PACKET(IS_H265_PACKET1),creationTime{creationTime}{
        // Validate correctness of NALU (make sure parser never forwards NALUs where this assertion fails)
        assert(hasValidPrefix());
        assert(getSize()>=getMinimumNaluSize(IS_H265_PACKET1));
        m_nalu_prefix_size=get_nalu_prefix_size();
    };
    // tmp
    NALU(const uint8_t* data1,size_t data_len1,const bool IS_H265_PACKET1=false,const std::chrono::steady_clock::time_point creationTime=std::chrono::steady_clock::now()):
            m_data(data1),m_data_len(data_len1),IS_H265_PACKET(IS_H265_PACKET1),creationTime{creationTime}
    {
        assert(hasValidPrefix());
        assert(getSize()>=getMinimumNaluSize(IS_H265_PACKET1));
        m_nalu_prefix_size=get_nalu_prefix_size();
    }
    // tmp, for data with 001 prefix instead of 0001
    ~NALU()= default;
private:
    // With the default constructor a NALU does not own its memory. This saves us one memcpy. However, storing a NALU after the lifetime of the
    // Non-owned memory expired is also needed in some places, so the copy-constructor creates a copy of the non-owned data and stores it in a optional buffer
    // WARNING: Order is important here (Initializer list). Declare before data pointer
    const std::optional<std::vector<uint8_t>> ownedData={};
    const uint8_t* m_data;
    const size_t m_data_len;
    int m_nalu_prefix_size;
public:
    const bool IS_H265_PACKET;
    // creation time is used to measure latency
    const std::chrono::steady_clock::time_point creationTime;
public:
    // returns true if starts with 0001, false otherwise
    bool hasValidPrefixLong()const{
        return m_data[0]==0 && m_data[1]==0 &&m_data[2]==0 &&m_data[3]==1;
    }
    // returns true if starts with 001 (short prefix), false otherwise
    bool hasValidPrefixShort()const{
        return m_data[0]==0 && m_data[1]==0 &&m_data[2]==1;
    }
    bool hasValidPrefix()const{
        return hasValidPrefixLong() || hasValidPrefixShort();
    }
    int get_nalu_prefix_size()const{
        if(hasValidPrefixLong())return 4;
        return 3;
    }
    static std::size_t getMinimumNaluSize(const bool isH265){
        // 4 bytes prefix, 1 byte header for h264, 2 byte header for h265
        return isH265 ? 6 : 5;
    }
public:
    // pointer to the NALU data with 0001 prefix
    const uint8_t* getData()const{
        return m_data;
    }
    // size of the NALU data with 0001 prefix
    size_t getSize()const{
        return m_data_len;
    }
    //pointer to the NALU data without 0001 prefix
    const uint8_t* getDataWithoutPrefix()const{
        return &getData()[m_nalu_prefix_size];
    }
    //size of the NALU data without 0001 prefix
    ssize_t getDataSizeWithoutPrefix()const{
        return getSize()-m_nalu_prefix_size;
    }
    // return the nal unit type (quick)
   int get_nal_unit_type()const{
       if(IS_H265_PACKET){
           return (getDataWithoutPrefix()[0] & 0x7E)>>1;
       }
       return getDataWithoutPrefix()[0]&0x1f;
   }
public:
   bool isSPS()const{
       return (get_nal_unit_type() == NALUnitType::H264::NAL_UNIT_TYPE_SPS);
   }
   bool isPPS()const{
       return (get_nal_unit_type() == NALUnitType::H264::NAL_UNIT_TYPE_PPS);
   }
   bool is_config(){
       return isSPS() || isPPS();
   }
};



#endif //LIVE_VIDEO_10MS_ANDROID_NALU_H
