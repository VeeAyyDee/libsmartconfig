/* SPDX-License-Identifier: 0BSD */
#include "sc_touch.h"
#include "sc_airkiss.h"
#include "sc_touch2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
static sc_scan_ap ap = {{2,1,2,3,4,5},{'x','y'},2,1,0};
static void triplet(sc_touch *ctx,uint8_t index,uint8_t value)
{
    uint8_t input[2]={value,index};unsigned int crc=sc_touch_crc8(input,2);
    (void)sc_touch_feed(ctx,(uint16_t)(40U+(crc&240U)+(value>>4)));
    (void)sc_touch_feed(ctx,(uint16_t)(296U+index));
    (void)sc_touch_feed(ctx,(uint16_t)(40U+((crc&15U)<<4)+(value&15U)));
}
static void v1(void)
{
    unsigned int mode,i;
    for(mode=0;mode<7;++mode){
        sc_touch *ctx=sc_touch_create();sc_touch_result result;sc_scan_ap hint=ap;
        uint8_t data[12]={12,1,0,0,0,192,0,2,1,'p','x','y'};
        data[2]=sc_touch_crc8(ap.ssid,2);data[3]=sc_touch_crc8(ap.bssid,6);
        for(i=0;i<12;++i)if(i!=4)data[4]^=data[i];
        if(mode==2)hint.ssid_len=1;
        if(mode==3)hint.ssid[0]='z';
        if(mode==4)hint.bssid[5]^=1;
        if(mode!=1)sc_touch_set_ap(ctx,&hint);
        if(mode==5)triplet(ctx,10,'z');
        if(mode==6)triplet(ctx,12,4);
        for(i=0;i<10;++i)triplet(ctx,(uint8_t)i,data[i]);
        CHECK(sc_touch_get_result(ctx,&result)==(mode==0));
        if(mode==0)CHECK(result.ssid_len==2&&memcmp(result.ssid,ap.ssid,2)==0&&memcmp(result.bssid,ap.bssid,6)==0);
        sc_touch_reset(ctx);
        for(i=0;i<10;++i)triplet(ctx,(uint8_t)i,data[i]);
        CHECK(!sc_touch_get_result(ctx,&result));
        sc_touch_destroy(ctx);
    }
}
static void ak_metadata(sc_airkiss *ctx,size_t p,size_t s)
{
    unsigned int total=(unsigned int)(p+s+1),crc=sc_touch_crc8(ap.ssid,s),high=total>>4;
    uint8_t pb=(uint8_t)p;unsigned int pc=sc_touch_crc8(&pb,1);
    uint16_t symbols[8]={(uint16_t)(high?high:8),(uint16_t)(16U+(total&15U)),(uint16_t)(32U+(crc>>4)),(uint16_t)(48U+(crc&15U)),(uint16_t)(64U+(p>>4)),(uint16_t)(80U+(p&15U)),(uint16_t)(96U+(pc>>4)),(uint16_t)(112U+(pc&15U))};
    size_t i;for(i=0;i<8;++i)(void)sc_airkiss_feed(ctx,symbols[i]);
}
static void airkiss(void)
{
    size_t p;unsigned int mode;
    for(p=0;p<=64;++p)for(mode=0;mode<5;++mode){
        sc_airkiss *ctx=sc_airkiss_create();sc_airkiss_result result;sc_scan_ap hint=ap;
        uint8_t data[97],crcdata[5];size_t i,offset,required=p+1;
        memset(data,'p',p);data[p]=0x93;memcpy(data+p+1,ap.ssid,ap.ssid_len);
        if(mode==2)hint.ssid[0]='z';
        if(mode!=1)sc_airkiss_set_ap(ctx,&hint);
        ak_metadata(ctx,p,ap.ssid_len);
        if(mode==3)required+=ap.ssid_len; /* Full sender remains accepted. */
        for(offset=0;offset<required;offset+=4){
            size_t count=required-offset;if(count>4)count=4;
            crcdata[0]=(uint8_t)(offset/4);memcpy(crcdata+1,data+offset,count);
            (void)sc_airkiss_feed(ctx,(uint16_t)(128U+((sc_touch_crc8(crcdata,count+1)&127U)^(mode==4?1U:0U))));
            (void)sc_airkiss_feed(ctx,(uint16_t)(128U+offset/4));
            for(i=0;i<count;++i)(void)sc_airkiss_feed(ctx,(uint16_t)(256U+data[offset+i]));
        }
        CHECK(sc_airkiss_get_result(ctx,&result)==(mode==0||mode==3));
        if(mode==0||mode==3)CHECK(result.password_len==p&&result.token==0x93&&result.ssid_len==ap.ssid_len&&memcmp(result.ssid,ap.ssid,ap.ssid_len)==0);
        sc_airkiss_destroy(ctx);
    }
}
static void ak_block(sc_airkiss *ctx,uint8_t index,const uint8_t *bytes,size_t n)
{
    uint8_t crcdata[5];size_t i;crcdata[0]=index;memcpy(crcdata+1,bytes,n);
    (void)sc_airkiss_feed(ctx,(uint16_t)(128U+(sc_touch_crc8(crcdata,n+1)&127U)));
    (void)sc_airkiss_feed(ctx,(uint16_t)(128U+index));
    for(i=0;i<n;++i)(void)sc_airkiss_feed(ctx,(uint16_t)(256U+bytes[i]));
}
static void late_airkiss_hint(void)
{
    unsigned int bad;
    for(bad=0;bad<2;++bad){
        sc_airkiss *ctx=sc_airkiss_create();sc_airkiss_result result;
        uint8_t suffix[2]={'x','y'},prefix[4]={'p','p','p',0x93};
        if(bad)suffix[0]='z';
        ak_metadata(ctx,3,2);ak_block(ctx,1,suffix,2);
        sc_airkiss_set_ap(ctx,&ap);ak_block(ctx,0,prefix,4);
        CHECK(sc_airkiss_get_result(ctx,&result)==!bad);sc_airkiss_destroy(ctx);
    }
}
static void planes(sc_touch2 *ctx,const uint8_t input[5])
{
    uint8_t bytes[6];unsigned int i,k;memcpy(bytes,input,5);bytes[5]=sc_touch_crc8(bytes,5);
    for(i=0;i<8;++i){unsigned int bits=0;for(k=0;k<6;++k)bits|=((unsigned int)(bytes[k]>>i)&1U)<<(5U-k);(void)sc_touch2_feed(ctx,(uint16_t)(64U|(i<<7)|bits));}
}
static void v2(void)
{
    unsigned int mode;
    for(mode=0;mode<5;++mode){
        sc_touch2 *ctx=sc_touch2_create(NULL);sc_touch2_result result;sc_scan_ap hint=ap;
        uint8_t header[5]={130,129,128,0,1},pwd[5]={'p'},bad[5]={'z','y'};
        header[3]=sc_touch_crc8(ap.bssid,6);
        if(mode==2)hint.ssid_len=1;
        if(mode==3)hint.bssid[5]^=1;
        if(mode!=1)sc_touch2_set_ap(ctx,&hint);
        (void)sc_touch2_feed(ctx,1048);(void)sc_touch2_feed(ctx,1074);(void)sc_touch2_feed(ctx,1048);(void)sc_touch2_feed(ctx,1074);planes(ctx,header);
        if(mode==4){(void)sc_touch2_feed(ctx,129);planes(ctx,bad);}
        (void)sc_touch2_feed(ctx,128);planes(ctx,pwd);
        CHECK(sc_touch2_get_result(ctx,&result)==(mode==0));
        if(mode==0)CHECK(result.ssid_len==2&&memcmp(result.ssid,ap.ssid,2)==0&&result.password[0]=='p');
        sc_touch2_destroy(ctx);
    }
}
int main(void){v1();airkiss();late_airkiss_hint();v2();puts("PASS: scan-assisted v1/v2 integrity and omitted-SSID AirKiss all password lengths/short/full blocks");return 0;}
