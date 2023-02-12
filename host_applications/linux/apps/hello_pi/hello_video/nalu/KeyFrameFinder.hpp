//
// Created by geier on 07/02/2020.
//

#ifndef LIVEVIDEO10MS_KEYFRAMEFINDER_HPP
#define LIVEVIDEO10MS_KEYFRAMEFINDER_HPP

#include "NALU.hpp"
#include <vector>
#include <memory>
#include <array>

// Takes a continuous stream of NALUs and save SPS / PPS data
// For later use
class KeyFrameFinder{
private:
    std::unique_ptr<NALU> SPS=nullptr;
    std::unique_ptr<NALU> PPS=nullptr;
public:
    bool saveIfKeyFrame(const NALU &nalu){
        if(nalu.getSize()<=0)return false;
        if(nalu.isSPS()){
            SPS=std::make_unique<NALU>(nalu);
            return true;
        }else if(nalu.isPPS()){
            PPS=std::make_unique<NALU>(nalu);
            return true;
        }
        //qDebug()<<"not a keyframe"<<(int)nalu.getDataWithoutPrefix()[0];
        return false;
    }
    // H264 needs sps and pps
    // H265 needs sps,pps and vps
    bool allKeyFramesAvailable(){
        return SPS != nullptr && PPS != nullptr;
    }
    // returns false if the config data (SPS,PPS,optional VPS) has changed
    // true otherwise
    bool check_is_still_same_config_data(const NALU &nalu){
	  if(!allKeyFramesAvailable())return true;
        if(nalu.isSPS()){
            return compare(nalu,*SPS);
        }else if(nalu.isPPS()){
            return compare(nalu,*PPS);
        }
        return true;
    }
    //SPS
    const NALU& getCSD0()const{
        return *SPS;
    }
    //PPS
    const NALU& getCSD1()const{
        return *PPS;
    }
    static void appendNaluData(std::vector<uint8_t>& buff,const NALU& nalu){
        buff.insert(buff.begin(),nalu.getData(),nalu.getData()+nalu.getSize());
    }
    void reset(){
        SPS=nullptr;
        PPS=nullptr;
    }
public:
    static bool compare(const NALU& n1,const NALU& n2){
        if(n1.getSize()!=n2.getSize())return false;
        const int res=std::memcmp(n1.getData(),n2.getData(),n1.getSize());
        return res==0;
    }

};

#endif //LIVEVIDEO10MS_KEYFRAMEFINDER_HPP
