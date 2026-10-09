/* SPDX-License-Identifier: 0BSD */
/* Deterministic sender clock with the real worker, Wi-Fi and event mocks. */
#define main compatibility_suite_main
#include "test_libsmartconfig_compat.c"
#undef main
/* Observe complete worker iterations rather than assuming a wall-clock sleep
 * lets the host scheduler run them. Virtual radio time stays fixed here. */
static void wait_worker_cycles(void)
{
    unsigned int before = atomic_load(&worker_cycles), i;
    for (i = 0; i < 1000 && atomic_load(&worker_cycles) - before < 2U; ++i)
        pause_ms(1);
    CHECK(atomic_load(&worker_cycles) - before >= 2U);
}
static void empty_credentials(bool ak)
{
    unsigned int i;
    uint8_t ssid='s',token=0x93;
    if(ak){
        unsigned int crc=sc_touch_crc8(&ssid,1);
        uint8_t block[2]={0,token};
        uint16_t metadata[8]={8,18,(uint16_t)(32U+(crc>>4)),(uint16_t)(48U+(crc&15U)),64,80,96,112};
        for(i=0;i<8;++i)packet(metadata[i]);
        packet((uint16_t)(128U+(sc_touch_crc8(block,2)&127U)));packet(128);packet((uint16_t)(256U+token));
    }else{
        uint8_t bytes[10]={10,0,0,0,0,192,0,2,1,'s'};
        bytes[2]=sc_touch_crc8(&ssid,1);bytes[3]=sc_touch_crc8(bssid,6);
        for(i=0;i<10;++i)if(i!=4)bytes[4]^=bytes[i];
        for(i=0;i<9;++i)triplet((uint8_t)i,bytes[i]);
    }
}
int main(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    unsigned int ak,combined,channels,phase;
    for(ak=0;ak<2;++ak)for(combined=0;combined<2;++combined)
    for(channels=2;channels<=4;++channels)for(phase=0;phase<4;++phase){
        unsigned int tick,heard=0,missed=0,retunes;
        uint8_t ap_channel;
        reset();atomic_store(&manual_clock,true);atomic_store(&manual_time,1000);
        CHECK(esp_smartconfig_fast_mode(true)==ESP_OK);
        CHECK(esp_smartconfig_set_type(combined?SC_TYPE_ESPTOUCH_AIRKISS:ak?SC_TYPE_AIRKISS:SC_TYPE_ESPTOUCH)==ESP_OK);
        scan_count=(uint16_t)channels;
        for(unsigned int i=1;i<channels;++i){scan_records[i]=scan_records[0];scan_records[i].primary=(uint8_t)(1U+3U*i);}
        for(unsigned int i=0;i<channels;++i)scan_records[i].bssid[5]=(uint8_t)(8U+i);
        memcpy(scan_records[channels-1].bssid,bssid,6);ap_channel=scan_records[channels-1].primary;
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
        for(tick=0;tick<80 && atomic_load(&posted_found)==0;++tick){
            uint8_t channel;
            atomic_store(&manual_time,1000U+tick*10U);wait_worker_cycles();
            CHECK(pthread_mutex_lock(&api_mutex)==0);channel=mock.channel;end();
            if(channel==ap_channel){packet((uint16_t)(ak?1U+(tick+phase)%4U:515U-(tick+phase)%4U));++heard;}else ++missed;
            wait_worker_cycles();
        }
        CHECK(atomic_load(&posted_found)==1&&heard==4&&missed>0);
        CHECK(pthread_mutex_lock(&api_mutex)==0);retunes=mock.channel_calls;CHECK(mock.channel==ap_channel);end();
        /* Move to the guide/data burst boundary (2s). A confirmed lock stays
         * on the AP despite many50ms deadlines; missing SSID comes from cache. */
        atomic_store(&manual_time,3000);wait_worker_cycles();
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel==ap_channel&&mock.channel_calls==retunes);end();
        empty_credentials(ak!=0);wait_for(&posted_credentials,1);
        CHECK(atomic_load(&posted_scan)==1&&atomic_load(&posted_found)==1);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.result.ssid[0]=='s'&&mock.result.password[0]==0);end();
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    puts("PASS:48 real-worker50ms hopper/10ms guide phase/channel cases, v1/AirKiss single/combined, frozen hop and omitted-SSID empty-password completion");
    return 0;
}
