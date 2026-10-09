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
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER, api_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool in_worker;
static atomic_bool task_failure, stop_on_event;
static atomic_uint tasks_alive, posted_found, posted_credentials, stop_event_calls;
static atomic_uint clock_offset, decrypt_calls;
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
    unsigned int channel_calls;
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
void vTaskDelay(unsigned int ticks) { pause_ms(ticks == 0 ? 1 : ticks); }
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
esp_err_t esp_wifi_get_mode(wifi_mode_t *v) { esp_err_t e=begin(GET_MODE); if(e==ESP_OK)*v=mock.mode;end();return e; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *v) { esp_err_t e=begin(GET_AP);(void)v;if(e==ESP_OK&&!mock.associated)e=ESP_ERR_WIFI_NOT_CONNECT;end();return e; }
esp_err_t esp_wifi_get_promiscuous(bool *v) { esp_err_t e=begin(GET_PROMISC);if(e==ESP_OK)*v=mock.promiscuous;end();return e; }
esp_err_t esp_wifi_get_country(wifi_country_t *v) { esp_err_t e=begin(GET_COUNTRY);if(e==ESP_OK){v->schan=1;v->nchan=11;}end();return e; }
esp_err_t esp_wifi_get_channel(uint8_t *c,wifi_second_chan_t *s) { esp_err_t e=begin(GET_CHANNEL);if(e==ESP_OK){*c=mock.channel;*s=mock.secondary;}end();return e; }
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *f) { esp_err_t e=begin(GET_FILTER);if(e==ESP_OK)*f=mock.filter;end();return e; }
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f) { esp_err_t e=begin(SET_FILTER);if(e==ESP_OK)mock.filter=*f;end();return e; }
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb) { esp_err_t e=begin(SET_CALLBACK);if(e==ESP_OK)mock.callback=cb;end();return e; }
esp_err_t esp_wifi_set_channel(uint8_t c,wifi_second_chan_t s) { esp_err_t e=begin(SET_CHANNEL);if(e==ESP_OK){mock.channel=c;mock.secondary=s;++mock.channel_calls;}end();return e; }
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
    CHECK(base==SC_EVENT && wait==0 && id!=SC_EVENT_SCAN_DONE && id!=SC_EVENT_SEND_ACK_DONE);
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
    reset();CHECK(strstr(esp_smartconfig_get_version(),"0.2.0")!=NULL);
    CHECK(esp_smartconfig_internal_start(NULL)==ESP_ERR_INVALID_ARG);
    config.esp_touch_v2_enable_crypt=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_ARG);config.esp_touch_v2_enable_crypt=false;
    CHECK(esp_smartconfig_set_type((smartconfig_type_t)4)==ESP_ERR_INVALID_ARG);
    CHECK(esp_esptouch_set_timeout(14)==ESP_ERR_INVALID_ARG);CHECK(esp_esptouch_set_timeout(255)==ESP_OK);CHECK(esp_esptouch_set_timeout(15)==ESP_OK);
    CHECK(esp_smartconfig_fast_mode(true)==ESP_ERR_NOT_SUPPORTED);CHECK(esp_smartconfig_fast_mode(false)==ESP_OK);
    CHECK(esp_smartconfig_get_rvd_data(out,33)==ESP_ERR_INVALID_STATE);CHECK(esp_smartconfig_get_rvd_data(NULL,0)==ESP_ERR_INVALID_ARG);
    for(i=GET_MODE;i<=SET_PROMISC;++i){
        reset();mock.fail_operation=(int)i;CHECK(esp_smartconfig_internal_start(&config)==ESP_FAIL);restored();
        CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_set_type(SC_TYPE_AIRKISS)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_fast_mode(false)==ESP_ERR_INVALID_STATE);CHECK(esp_esptouch_set_timeout(15)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    reset();atomic_store(&task_failure,true);CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_NO_MEM);restored();
    for(i=SET_FILTER;i<=SET_PROMISC;++i){
        reset();CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
        CHECK(pthread_mutex_lock(&api_mutex)==0);mock.fail_operation=(int)i;end();
        CHECK(esp_smartconfig_internal_stop()==ESP_FAIL);
        CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);
        CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
    }
    reset();mock.associated=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);mock.associated=false;
    mock.promiscuous=true;CHECK(esp_smartconfig_internal_start(&config)==ESP_ERR_INVALID_STATE);mock.promiscuous=false;
    mock.mode=WIFI_MODE_APSTA;CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
    atomic_store(&clock_offset,1000);pause_ms(25);
    CHECK(pthread_mutex_lock(&api_mutex)==0);CHECK(mock.channel_calls==0&&mock.channel==7);end();
    CHECK(esp_smartconfig_internal_stop()==ESP_OK);restored();
}
static uint16_t sequence;
static const uint8_t bssid[6]={2,1,2,3,4,5};
static void packet(uint16_t length)
{
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
    packet((uint16_t)(128U+(sc_touch_crc8(bytes,4)&127U)));packet(128);
    for(i=1;i<4;++i)packet((uint16_t)(256U+bytes[i]));
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
    for(i=0;i<17;++i)triplet((uint8_t)i,data[i]);
}
static void protocols(void)
{
    smartconfig_start_config_t config=SMARTCONFIG_START_CONFIG_DEFAULT();unsigned int type,which;
    for(type=0;type<3;++type)for(which=0;which<2;++which){
        if(type!=2&&type!=which)continue;
        reset();CHECK(esp_smartconfig_set_type((smartconfig_type_t)type)==ESP_OK);CHECK(esp_smartconfig_internal_start(&config)==ESP_OK);
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
    wait_for(&posted_found,1);atomic_fetch_add(&clock_offset,61000);pause_ms(25);touch();
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
    for(i=0;i<group_count;++i){packet((uint16_t)(128U+i));v2_planes(groups[i]);}
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
        if(pass==2){pause_ms(40);CHECK(atomic_load(&posted_credentials)==0&&atomic_load(&decrypt_calls)==1);}
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
int main(void)
{
    lifecycle();protocols();v2_options();
    puts("PASS: standard compatibility real-worker lifecycle/rollback/options, single/combined capture, event order/retry/handler-stop, APSTA channel and timeout reset");
    return 0;
}
