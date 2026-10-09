/* SPDX-License-Identifier: 0BSD */
#define _POSIX_C_SOURCE 200809L
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sc_touch.h"
#include "sc_touch2_psa.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
esp_err_t esp_smartconfig_internal_start(const smartconfig_start_config_t *config);
esp_err_t esp_smartconfig_internal_stop(void);
ESP_EVENT_DEFINE_BASE(SC_EVENT);
ESP_EVENT_DEFINE_BASE(WIFI_EVENT);
static esp_event_handler_t scan_handler, fence_handler;
static atomic_uint posted_scan, scan_calls, scan_stop_calls, disconnect_calls;
static atomic_bool auto_scan = true, fail_scan_start, fail_disconnect;
static atomic_int fail_register;
static wifi_ap_record_t scan_records[4];
static uint16_t scan_count, scan_index;
static esp_event_base_t fence_base;
static void scan_complete(uint32_t status)
{
    wifi_event_sta_scan_done_t event = {.status=status};
    CHECK(scan_handler != NULL); scan_handler(NULL, WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &event);
}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg, esp_event_handler_instance_t *instance)
{
    (void)id;(void)arg;
    if(atomic_load(&fail_register)==(base==WIFI_EVENT?1:2)){atomic_store(&fail_register,0);return ESP_ERR_NO_MEM;}
    if(base==WIFI_EVENT)scan_handler=handler;else{fence_handler=handler;fence_base=base;}
    *instance=(void *)1;return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance)
{
    (void)id;(void)instance;
    if(base==WIFI_EVENT)scan_handler=NULL;else fence_handler=NULL;
    return ESP_OK;
}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    CHECK(config->show_hidden&&!block);scan_index=0;atomic_fetch_add(&scan_calls,1);
    if(atomic_exchange(&fail_scan_start,false))return ESP_ERR_INVALID_STATE;
    if(atomic_load(&auto_scan))scan_complete(0);
    return ESP_OK;
}
esp_err_t esp_wifi_scan_stop(void){atomic_fetch_add(&scan_stop_calls,1);return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number){*number=scan_count;return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record){CHECK(scan_index<scan_count);*record=scan_records[scan_index++];return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(void){return ESP_OK;}

static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER, api_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool in_worker;
static atomic_bool task_failure, stop_on_event;
static atomic_uint tasks_alive, posted_found, posted_credentials, stop_event_calls;
static atomic_uint clock_offset, decrypt_calls, manual_time, worker_cycles;
static atomic_bool manual_clock, hold_fence;
static uint32_t held_generation;
static atomic_bool decrypt_failure;
static atomic_int event_error;
static int task_identity;
static struct {
    wifi_mode_t mode;
    bool associated, promiscuous;
    uint8_t channel;
    wifi_second_chan_t secondary;
    wifi_promiscuous_filter_t filter;
    wifi_promiscuous_cb_t callback;
    int fail_operation;
    unsigned int channel_calls, channel_attempts;
    smartconfig_event_got_ssid_pswd_t result;
} mock;
enum { GET_MODE=1, GET_AP, GET_PROMISC, GET_COUNTRY, GET_CHANNEL, GET_FILTER,
       SET_FILTER, SET_CALLBACK, SET_CHANNEL, SET_PROMISC };
static void pause_ms(unsigned int ms)
{ struct timespec t = {(time_t)(ms / 1000U), (long)(ms % 1000U) * 1000000L}; (void)nanosleep(&t, NULL); }
void test_enter_critical(portMUX_TYPE *m) { (void)m; CHECK(pthread_mutex_lock(&critical) == 0); }
void test_exit_critical(portMUX_TYPE *m) { (void)m; CHECK(pthread_mutex_unlock(&critical) == 0); }
int64_t esp_timer_get_time(void)
{
    if(atomic_load(&manual_clock))return (int64_t)atomic_load(&manual_time)*1000;
    struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (int64_t)t.tv_sec * 1000000 + (int64_t)t.tv_nsec / 1000 + (int64_t)atomic_load(&clock_offset) * 1000;
}
typedef struct { TaskFunction_t function; void *arg; } task_call;
static void *task_entry(void *opaque)
{
    task_call call = *(task_call *)opaque; free(opaque); in_worker = true;
    call.function(call.arg); CHECK(false); return NULL;
}
int xTaskCreate(TaskFunction_t fn, const char *name, unsigned int size, void *arg, unsigned int priority, TaskHandle_t *handle)
{
    pthread_t thread; task_call *call;
    CHECK(strcmp(name,"smartconfig") == 0 && size >= 4096 && priority > 0);
    if (atomic_exchange(&task_failure, false)) return 0;
    call = malloc(sizeof(*call)); CHECK(call != NULL); call->function = fn; call->arg = arg;
    *handle = &task_identity; atomic_fetch_add(&tasks_alive, 1);
    CHECK(pthread_create(&thread, NULL, task_entry, call) == 0);
    CHECK(pthread_detach(thread) == 0); return pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return in_worker ? &task_identity : NULL; }
void vTaskDelay(unsigned int ticks)
{
    if (in_worker) atomic_fetch_add(&worker_cycles, 1);
    pause_ms(ticks == 0 ? 1 : ticks);
}
void vTaskDelete(TaskHandle_t handle)
{ CHECK(handle == NULL && in_worker); atomic_fetch_sub(&tasks_alive, 1); pthread_exit(NULL); }
static esp_err_t begin(unsigned int operation)
{
    esp_err_t result = ESP_OK;
    CHECK(pthread_mutex_lock(&api_mutex) == 0);
    if (mock.fail_operation == (int)operation) { mock.fail_operation = 0; result = ESP_FAIL; }
    return result;
}
static void end(void) { CHECK(pthread_mutex_unlock(&api_mutex) == 0); }
esp_err_t esp_wifi_disconnect(void){if(atomic_exchange(&fail_disconnect,false)){atomic_fetch_add(&disconnect_calls,1);return ESP_FAIL;}CHECK(pthread_mutex_lock(&api_mutex)==0);mock.associated=false;end();atomic_fetch_add(&disconnect_calls,1);return ESP_OK;}
esp_err_t esp_wifi_get_mode(wifi_mode_t *v) { esp_err_t e=begin(GET_MODE); if(e==ESP_OK)*v=mock.mode;end();return e; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *v) { esp_err_t e=begin(GET_AP);(void)v;if(e==ESP_OK&&!mock.associated)e=ESP_ERR_WIFI_NOT_CONNECT;end();return e; }
esp_err_t esp_wifi_get_promiscuous(bool *v) { esp_err_t e=begin(GET_PROMISC);if(e==ESP_OK)*v=mock.promiscuous;end();return e; }
esp_err_t esp_wifi_get_country(wifi_country_t *v) { esp_err_t e=begin(GET_COUNTRY);if(e==ESP_OK){v->schan=1;v->nchan=11;}end();return e; }
esp_err_t esp_wifi_get_channel(uint8_t *c,wifi_second_chan_t *s) { esp_err_t e=begin(GET_CHANNEL);if(e==ESP_OK){*c=mock.channel;*s=mock.secondary;}end();return e; }
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *f) { esp_err_t e=begin(GET_FILTER);if(e==ESP_OK)*f=mock.filter;end();return e; }
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f) { esp_err_t e=begin(SET_FILTER);if(e==ESP_OK)mock.filter=*f;end();return e; }
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb) { esp_err_t e=begin(SET_CALLBACK);if(e==ESP_OK)mock.callback=cb;end();return e; }
esp_err_t esp_wifi_set_channel(uint8_t c,wifi_second_chan_t s) { esp_err_t e=begin(SET_CHANNEL);++mock.channel_attempts;if(e==ESP_OK){mock.channel=c;mock.secondary=s;++mock.channel_calls;}end();return e; }
esp_err_t esp_wifi_set_promiscuous(bool v) { esp_err_t e=begin(SET_PROMISC);if(e==ESP_OK)mock.promiscuous=v;end();return e; }
int sc_touch2_psa_decrypt(void *u,const uint8_t key[16],const uint8_t iv[16],const uint8_t *cipher,size_t n,uint8_t *plain)
{
    size_t i;(void)u;CHECK(n==16);
    for(i=0;i<16;++i)CHECK(key[i]==i&&iv[i]==0&&cipher[i]==0x80U+i);
    memset(plain,13,n);plain[0]='p';plain[1]='r';plain[2]='z';
    atomic_fetch_add(&decrypt_calls,1);return atomic_load(&decrypt_failure)?0:1;
}
static void *stop_handler(void *unused)
{
    esp_err_t error;
    (void)unused;
    do { error=esp_smartconfig_internal_stop(); if(error==ESP_ERR_INVALID_STATE)pause_ms(1); } while(error==ESP_ERR_INVALID_STATE);
    CHECK(error==ESP_OK); atomic_fetch_add(&stop_event_calls,1); return NULL;
}
esp_err_t esp_event_post(esp_event_base_t base,int32_t id,const void *data,size_t bytes,unsigned int wait)
{
    if(base==fence_base){CHECK(fence_handler!=NULL&&bytes==sizeof(uint32_t));if(atomic_load(&hold_fence))memcpy(&held_generation,data,bytes);else fence_handler(NULL,base,id,(void *)data);return ESP_OK;}
    CHECK(base==SC_EVENT && wait==0 && id!=SC_EVENT_SEND_ACK_DONE);
    if(id==SC_EVENT_SCAN_DONE){CHECK(data==NULL&&bytes==0);atomic_fetch_add(&posted_scan,1);return ESP_OK;}
    CHECK(atomic_load(&posted_scan)>0);
    if (atomic_exchange(&event_error,0)!=0) return ESP_ERR_TIMEOUT;
    if(id==SC_EVENT_FOUND_CHANNEL){CHECK(data==NULL&&bytes==0);atomic_fetch_add(&posted_found,1);}
    else {
        CHECK(id==SC_EVENT_GOT_SSID_PSWD && bytes==sizeof(mock.result) && atomic_load(&posted_found)>0);
        CHECK(pthread_mutex_lock(&api_mutex)==0); CHECK(!mock.promiscuous);memcpy(&mock.result,data,bytes);end();
        atomic_fetch_add(&posted_credentials,1);
        if(atomic_exchange(&stop_on_event,false)) {
            pthread_t thread;CHECK(pthread_create(&thread,NULL,stop_handler,NULL)==0);CHECK(pthread_detach(thread)==0);
        }
    }
    return ESP_OK;
}
static void wait_for(atomic_uint *v,unsigned int expected)
{ unsigned int i; for(i=0;i<1000&&atomic_load(v)!=expected;++i)pause_ms(1);if(atomic_load(v)!=expected)fprintf(stderr,"wait actual=%u expected=%u found=%u creds=%u tasks=%u\n",atomic_load(v),expected,atomic_load(&posted_found),atomic_load(&posted_credentials),atomic_load(&tasks_alive));CHECK(atomic_load(v)==expected); }
static void reset(void)
{
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);wait_for(&tasks_alive,0);
    CHECK(pthread_mutex_lock(&api_mutex)==0);memset(&mock,0,sizeof(mock));mock.mode=WIFI_MODE_STA;mock.channel=7;mock.secondary=WIFI_SECOND_CHAN_ABOVE;mock.filter.filter_mask=99;end();
    atomic_store(&manual_clock,false);atomic_store(&hold_fence,false);held_generation=0;
    atomic_store(&posted_scan,0);atomic_store(&scan_calls,0);atomic_store(&scan_stop_calls,0);atomic_store(&disconnect_calls,0);atomic_store(&auto_scan,true);
    memset(scan_records,0,sizeof(scan_records));scan_count=1;scan_records[0].primary=1;scan_records[0].rssi=-40;scan_records[0].ssid[0]='s';
    { const uint8_t address[6]={2,1,2,3,4,5};memcpy(scan_records[0].bssid,address,6); }
    atomic_store(&posted_found,0);atomic_store(&posted_credentials,0);atomic_store(&stop_event_calls,0);atomic_store(&clock_offset,0);
    atomic_store(&stop_on_event,false);atomic_store(&event_error,0);
    CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS)==ESP_OK);
}
static void restored(void)
{
    CHECK(pthread_mutex_lock(&api_mutex)==0);
    CHECK(!mock.promiscuous&&mock.callback==NULL&&mock.filter.filter_mask==99&&mock.channel==7&&mock.secondary==WIFI_SECOND_CHAN_ABOVE);end();
    wait_for(&tasks_alive,0);
}
static void lifecycle(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();unsigned int i;uint8_t out[64];
    reset();CHECK(strstr(esp_smartconfig_get_version(),"0.3.0")!=NULL);
    CHECK(esp_smartconfig_internal_start(NULL)==ESP_ERR_INVALID_ARG);
    config.esp_touch_v2_enable_crypt=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_ARG);config.esp_touch_v2_enable_crypt=false;
    CHECK(esp_smartconfig_set_type((smartconfig_type_t)4)==ESP_ERR_INVALID_ARG);
    CHECK(esp_esptouch_set_timeout(14)==ESP_ERR_INVALID_ARG);CHECK(esp_esptouch_set_timeout(15)==ESP_OK);
    CHECK(esp_smartconfig_fast_mode(true)==ESP_OK);CHECK(esp_smartconfig_fast_mode(false)==ESP_OK);
    CHECK(esp_smartconfig_get_rvd_data(out,33)==ESP_ERR_INVALID_STATE);
    for(i=GET_MODE;i<=SET_CALLBACK;++i){
        if(i==GET_AP)continue;
        reset();mock.fail_operation=(int)i;CHECK(esp_smartconfig_internal_start(&config)==ESP_FAIL);restored();
    }
    for(i=1;i<=2;++i){reset();atomic_store(&fail_register,(int)i);CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_NO_MEM);CHECK(scan_handler==NULL&&fence_handler==NULL);restored();}
    reset();atomic_store(&task_failure,true);CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_NO_MEM);restored();
    for(i=SET_FILTER;i<=SET_PROMISC;++i){
        reset();CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_fast_mode(false)==ESP_ERR_INVALID_STATE);
        CHECK(pthread_mutex_lock(&api_mutex)==0);mock.fail_operation=(int)i;end();
        CHECK(esp_smartconfig_internal_stop()==ESP_FAIL);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    reset();mock.mode=WIFI_MODE_AP;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);CHECK(atomic_load(&disconnect_calls)==0);restored();
    reset();atomic_store(&fail_disconnect,true);CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
    CHECK(atomic_load(&disconnect_calls)==1);CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    reset();mock.associated=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
    CHECK(!mock.associated&&atomic_load(&disconnect_calls)==1);CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    reset();mock.promiscuous=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);mock.promiscuous=false;
    mock.mode=WIFI_MODE_APSTA;CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
    atomic_store(&clock_offset,1000);pause_ms(25);
    CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls>=2&&mock.channel==1);end();
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    /* An owned in-flight scan is cancelled; empty or weak-only discoveries do
     * not publish scan completion, and a later valid pass enables capture. */
    reset();atomic_store(&auto_scan,false);atomic_store(&fail_scan_start,true);CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
    wait_for(&scan_calls,1);CHECK(esp_smartconfig_internal_stop()==ESP_OK);CHECK(atomic_load(&scan_stop_calls)==0);restored();
    reset();atomic_store(&auto_scan,false);CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
    wait_for(&scan_calls,1);CHECK(atomic_load(&posted_scan)==0);
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);CHECK(atomic_load(&scan_stop_calls)==1);restored();
    reset();atomic_store(&auto_scan,false);scan_records[0].rssi=-85;
    CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&scan_calls,1);scan_complete(0);
    wait_for(&scan_calls,2);scan_complete(0);wait_for(&scan_calls,3);CHECK(atomic_load(&posted_scan)==0);
    scan_records[0].rssi=-84;scan_complete(0);wait_for(&posted_scan,1);
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
}
static void discovery_boundaries(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    unsigned int fast;
    for(fast=0;fast<2;++fast){
        unsigned int before;
        reset();atomic_store(&manual_clock,true);atomic_store(&manual_time,1000);
        CHECK(esp_smartconfig_fast_mode(fast!=0)==ESP_OK);
        scan_count=2;scan_records[1]=scan_records[0];scan_records[1].bssid[5]=9;scan_records[1].primary=11;
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
        CHECK(pthread_mutex_lock(&api_mutex)==0);before=mock.channel_calls;CHECK(mock.channel==1);end();
        atomic_fetch_add(&manual_time,fast?49:99);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==before);end();
        atomic_fetch_add(&manual_time,1);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel==11&&mock.channel_calls==before+1);mock.fail_operation=SET_CHANNEL;end();
        atomic_fetch_add(&manual_time,fast?50:100);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel==11&&mock.channel_attempts==before+2);end();
        pause_ms(30);CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_attempts==before+2);end();
        atomic_fetch_add(&manual_time,fast?50:100);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel==1);end();
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,2);
        CHECK(pthread_mutex_lock(&api_mutex)==0);before=mock.channel_calls;end();
        atomic_fetch_add(&manual_time,fast?49:99);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==before);end();
        atomic_fetch_add(&manual_time,1);pause_ms(30);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==before+1);end();
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    reset();CHECK(esp_smartconfig_fast_mode(false)==ESP_OK);atomic_store(&auto_scan,false);atomic_store(&hold_fence,true);
    CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);pause_ms(40);
    CHECK(atomic_load(&scan_calls)==0);scan_complete(0);pause_ms(25);CHECK(atomic_load(&posted_scan)==0);
    atomic_store(&hold_fence,false);fence_handler(NULL,fence_base,0,&held_generation);
    wait_for(&scan_calls,1);scan_complete(1);wait_for(&scan_calls,2);CHECK(atomic_load(&posted_scan)==0);
    scan_complete(0);wait_for(&scan_calls,3);scan_complete(0);wait_for(&posted_scan,1);
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
}
static bool omit_ssid;
static uint16_t sequence;
static const uint8_t bssid[6]={2,1,2,3,4,5};
static void packet(uint16_t length)
{
    while(atomic_load(&posted_scan)==0)pause_ms(1);
    wifi_promiscuous_pkt_t p={0};wifi_promiscuous_cb_t callback;uint16_t seq=(uint16_t)((sequence++&4095U)<<4);
    CHECK(pthread_mutex_lock(&api_mutex)==0);callback=mock.callback;p.rx_ctrl.channel=mock.channel;end();CHECK(callback!=NULL);
    p.rx_ctrl.sig_len=(uint16_t)(length+100U);p.payload[0]=8;p.payload[1]=2;
    memset(p.payload+4,255,6);memcpy(p.payload+10,bssid,6);p.payload[16]=4;p.payload[22]=(uint8_t)seq;p.payload[23]=(uint8_t)(seq>>8);
    callback(&p,WIFI_PKT_DATA);
}
static void airkiss(void)
{
    uint8_t pwd=1,bytes[4]={0,'p',0x93,'s'};unsigned int crc=sc_touch_crc8(&pwd,1),i;
    uint16_t metadata[8]={8,19,0,0,64,81,0,0};uint8_t ssid='s';unsigned int s_crc=sc_touch_crc8(&ssid,1);
    for(i=0;i<8;++i)packet((uint16_t)(1U+i%4U));
    metadata[2]=(uint16_t)(32U+(s_crc>>4));metadata[3]=(uint16_t)(48U+(s_crc&15U));
    metadata[6]=(uint16_t)(96U+(crc>>4));metadata[7]=(uint16_t)(112U+(crc&15U));
    for(i=0;i<8;++i)packet(metadata[i]);
    packet((uint16_t)(128U+(sc_touch_crc8(bytes,omit_ssid?3U:4U)&127U)));packet(128);
    for(i=1;i<(omit_ssid?3U:4U);++i)packet((uint16_t)(256U+bytes[i]));
}
static void triplet(uint8_t index,uint8_t value)
{
    uint8_t crcbytes[2]={value,index};unsigned int crc=sc_touch_crc8(crcbytes,2);
    packet((uint16_t)(40U+(crc&0xf0U)+(value>>4)));
    packet((uint16_t)(40U+256U+index));packet((uint16_t)(40U+((crc&15U)<<4)+(value&15U)));
}
static void touch(void)
{
    uint8_t data[17]={11,1,0,0,0,192,0,2,1,'p','s',2,1,2,3,4,5};unsigned int i;uint8_t checksum=0;
    data[2]=sc_touch_crc8(data+10,1);data[3]=sc_touch_crc8(bssid,6);
    for(i=0;i<11;++i)checksum^=data[i];
    data[4]=checksum;
    for(i=0;i<8;++i)packet((uint16_t)(515U-i%4U));
    for(i=0;i<(omit_ssid?10U:17U);++i)triplet((uint8_t)i,data[i]);
}
static void protocols(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();unsigned int type,which,fast;
    for(fast=0;fast<2;++fast)for(type=0;type<3;++type)for(which=0;which<2;++which){
        if(type!=2&&type!=which)continue;
        reset();CHECK(esp_smartconfig_fast_mode(fast!=0)==ESP_OK);CHECK(esp_smartconfig_set_type((smartconfig_type_t)type)==ESP_OK);CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
        atomic_store(&event_error,1);if(which==0)touch();else airkiss();
        wait_for(&posted_credentials,1);pause_ms(25);CHECK(atomic_load(&posted_found)==1&&atomic_load(&posted_credentials)==1);
        CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.result.type==(smartconfig_type_t)which&&mock.result.ssid[0]=='s'&&mock.result.password[0]=='p'&&mock.result.bssid_set&&memcmp(mock.result.bssid,bssid,6)==0);end();
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    reset();CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);atomic_store(&stop_on_event,true);airkiss();
    wait_for(&stop_event_calls,1);restored();
    CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    /* Timeout resets a locked attempt, then combined mode can select another protocol. */
    reset();CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
    { unsigned int i;for(i=0;i<8;++i)packet((uint16_t)(1U+i%4U)); }
    wait_for(&posted_found,1);atomic_fetch_add(&clock_offset,61000);wait_for(&posted_scan,2);touch();
    wait_for(&posted_credentials,1);CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
}
static void v2_planes(const uint8_t input[5])
{
    uint8_t bytes[6];unsigned int i,k;
    memcpy(bytes,input,5);bytes[5]=sc_touch_crc8(bytes,5);
    for(i=0;i<8;++i){
        unsigned int data=0;
        for(k=0;k<6;++k)data|=((unsigned int)(bytes[k]>>i)&1U)<<(5U-k);
        packet((uint16_t)(64U|(i<<7)|data));
    }
}
static void v2_message(bool encrypted)
{
    uint8_t header[5]={129,129,130,0,9};
    uint8_t groups[5][5]={{'p',0,0,0,0},{'r','z',0,0,0},{'s',0,0,0,0},{0},{'s',0,0,0,0}};
    size_t group_count=encrypted?5:3,i,j;
    header[3]=sc_touch_crc8(bssid,6);if(encrypted)header[4]=11;
    if(encrypted)for(i=0;i<4;++i)for(j=0;j<5;++j)groups[i][j]=(uint8_t)(i*5+j<16?0x80U+i*5+j:0);
    packet(1048);packet((uint16_t)(1072U+group_count));packet(1048);packet((uint16_t)(1072U+group_count));
    v2_planes(header);
    for(i=0;i<group_count-(omit_ssid?1U:0U);++i){packet((uint16_t)(128U+i));v2_planes(groups[i]);}
}
static void v2_options(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    uint8_t output[64],key[16];unsigned int pass,i;
    for(pass=0;pass<3;++pass){
        reset();atomic_store(&decrypt_failure,pass==2);atomic_store(&decrypt_calls,0);
        for(i=0;i<16;++i)key[i]=(uint8_t)i;
        config.esp_touch_v2_enable_crypt=pass!=0;config.esp_touch_v2_key=(char *)key;
        CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2)==ESP_OK);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);memset(key,255,16);
        v2_message(pass!=0);
        if(pass==2){pause_ms(40);CHECK(atomic_load(&posted_credentials)==0&&atomic_load(&decrypt_calls)>=1);}
        else {
            wait_for(&posted_credentials,1);
            memset(output,0xff,sizeof(output));CHECK(esp_smartconfig_get_rvd_data(output,33)==ESP_OK);
            CHECK(output[0]=='r'&&output[1]=='z');for(i=2;i<33;++i)CHECK(output[i]==0);
            CHECK(esp_smartconfig_get_rvd_data(output,65)==ESP_ERR_INVALID_ARG);
            CHECK(pthread_mutex_lock(&api_mutex)==0);
            CHECK(mock.result.type==SC_TYPE_ESPTOUCH_V2&&mock.result.token==1&&mock.result.password[0]=='p'&&mock.result.ssid[0]=='s');
            /* Stop must preserve the channel of a newly associated station. */
            mock.associated=true;mock.channel=6;end();
        }
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);wait_for(&tasks_alive,0);
        CHECK(esp_smartconfig_get_rvd_data(output,33)==ESP_ERR_INVALID_STATE);
    }
}
static void omitted_workflow(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    unsigned int protocol,bad;uint8_t key[16];
    omit_ssid=true;
    for(protocol=0;protocol<4;++protocol)for(bad=0;bad<7;++bad){
        unsigned int type=protocol==2?3:protocol==3?3:protocol;
        reset();atomic_store(&decrypt_failure,false);
        if(bad==1)scan_records[0].ssid[0]=0;
        if(bad==2)scan_records[0].bssid[5]^=1;
        if(bad==3)scan_records[0].ssid[1]='x';
        if(bad==4)scan_records[0].pairwise_cipher=4;
        if(bad==5){scan_count=2;scan_records[1]=scan_records[0];scan_records[1].ssid[0]='z';}
        if(bad==6 && type!=3)scan_records[0].ssid[0]='z';
        if(bad==6 && type==3)scan_records[0].bssid[5]^=2;
        for(unsigned int i=0;i<16;++i)key[i]=(uint8_t)i;
        config.esp_touch_v2_enable_crypt=protocol==3;config.esp_touch_v2_key=(char *)key;
        CHECK(esp_smartconfig_set_type((smartconfig_type_t)type)==ESP_OK);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
        if(type==0)touch();else if(type==1)airkiss();else v2_message(protocol==3);
        if(bad==0)wait_for(&posted_credentials,1);
        else {pause_ms(45);CHECK(atomic_load(&posted_credentials)==0);}
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    omit_ssid=false;
}
static void initial_dwell(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();
    reset();atomic_store(&manual_clock,true);atomic_store(&manual_time,1000);
    CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);wait_for(&posted_scan,1);
    atomic_fetch_add(&manual_time,149);pause_ms(30);
    CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==1);end();
    atomic_fetch_add(&manual_time,1);pause_ms(30);
    CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==2);end();
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
}
int main(void)
{
    initial_dwell();lifecycle();discovery_boundaries();protocols();v2_options();omitted_workflow();
    puts("PASS: standard compatibility real-worker lifecycle/rollback/options, single/combined capture, event order/retry/handler-stop, APSTA channel and timeout reset");
    return 0;
}
