/* SPDX-License-Identifier: 0BSD */
/* Isolated real-worker timing probe, reusing owned Wi-Fi/event/task mocks. */
#define main compatibility_suite_main
#include "test_libsmartconfig_compat.c"
#undef main
int main(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    unsigned int fast,channels,phase;
    for(fast=0;fast<2;++fast)for(channels=2;channels<=4;++channels)for(phase=0;phase<4;++phase){
        unsigned int tick,heard=0,missed=0;
        reset();atomic_store(&manual_clock,true);atomic_store(&manual_time,1000);
        CHECK(esp_smartconfig_fast_mode(fast!=0)==ESP_OK);
        CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH)==ESP_OK);
        scan_count=(uint16_t)channels;
        for(unsigned int i=1;i<channels;++i){scan_records[i]=scan_records[0];scan_records[i].bssid[5]=(uint8_t)(5U+i);scan_records[i].primary=(uint8_t)(1U+3U*i);}
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
        /* Advance actual worker deadlines, let it retune, then deliver only
         * packets heard on the AP channel. The phone keeps sending off-channel. */
        for(tick=0;tick<80 && atomic_load(&posted_found)==0;++tick){
            uint8_t channel;
            atomic_store(&manual_time,1000U+tick*10U);pause_ms(12);
            CHECK(pthread_mutex_lock(&api_mutex)==0);channel=mock.channel;end();
            if(channel==1){packet((uint16_t)(515U-(tick+phase)%4U));++heard;}else ++missed;
            pause_ms(12);
        }
        printf("dwell=%u channels=%u phase=%u elapsed=%u heard=%u offchannel=%u found=%u\n",fast?50U:100U,channels,phase,tick*10U,heard,missed,atomic_load(&posted_found));
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    return 0;
}
