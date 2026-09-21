#include "web_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "vault_store.h"

extern const unsigned char web_index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const unsigned char web_index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const unsigned char web_logo_png_start[] asm("_binary_cipherport_logo_png_start");
extern const unsigned char web_logo_png_end[] asm("_binary_cipherport_logo_png_end");

typedef enum { CMD_START=1, CMD_STOP, CMD_APPROVE, CMD_DENY } command_kind_t;
typedef struct { command_kind_t kind; } command_t;
typedef enum { MUT_NONE=0, MUT_ADD, MUT_EDIT, MUT_DELETE } mutation_kind_t;
typedef struct { mutation_kind_t kind; size_t index; vault_account_t account; uint32_t id; } mutation_t;

static QueueHandle_t s_commands;
static QueueHandle_t s_events;
static SemaphoreHandle_t s_mutation_lock;
static portMUX_TYPE s_status_lock=portMUX_INITIALIZER_UNLOCKED;
static web_manager_status_t s_status={.state=WEB_MANAGER_OFF};
static httpd_handle_t s_server;
static esp_netif_t *s_ap;
static bool s_wifi;
static mutation_t s_pending;
static char s_token[33];
static const char *s_result="none";
static bool s_browser_announced;
static int64_t s_started_us;
static int64_t s_activity_us;
static void event_send(web_manager_event_type_t type,const char *platform);
static bool json_string(cJSON *root,const char *name,char *output,size_t capacity,bool required);

static void wifi_event(void *argument,esp_event_base_t base,int32_t id,void *data)
{
    (void)argument;(void)base;(void)data;
    if(id==WIFI_EVENT_AP_STACONNECTED){
        taskENTER_CRITICAL(&s_status_lock);
        s_status.client_connected=true;
        s_status.seconds_remaining=0U;
        taskEXIT_CRITICAL(&s_status_lock);
        return;
    }
    if(id!=WIFI_EVENT_AP_STADISCONNECTED)return;
    /* A phone may briefly roam or renew its link while the browser remains
       open. Keep the device-authorized session alive so the same browser can
       resume. Wi-Fi is stopped only by an explicit device-side exit/lock. */
    taskENTER_CRITICAL(&s_status_lock);
    s_status.client_connected=false;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void wipe(void *memory,size_t size){volatile uint8_t *p=memory;while(size--)*p++=0;}
static void event_send(web_manager_event_type_t type,const char *platform){
    if(!s_events)return;
    web_manager_event_t event={.type=type};
    if(platform)snprintf(event.platform,sizeof(event.platform),"%s",platform);
    (void)xQueueSend(s_events,&event,0);
}
static void headers(httpd_req_t *request){
    httpd_resp_set_hdr(request,"Cache-Control","no-store, max-age=0");
    httpd_resp_set_hdr(request,"Pragma","no-cache");
    httpd_resp_set_hdr(request,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(request,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(request,"Content-Security-Policy","default-src 'self'; img-src 'self' data:; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
}
static esp_err_t json(httpd_req_t *request,const char *body){headers(request);httpd_resp_set_type(request,"application/json; charset=utf-8");return httpd_resp_sendstr(request,body);}
static esp_err_t body_read(httpd_req_t *request,char *body,size_t capacity){
    if(request->content_len<=0||request->content_len>=(int)capacity)return ESP_ERR_INVALID_SIZE;
    size_t total=0U;unsigned timeouts=0U;
    while(total<(size_t)request->content_len){
        int got=httpd_req_recv(request,body+total,(size_t)request->content_len-total);
        if(got==HTTPD_SOCK_ERR_TIMEOUT){if(++timeouts<=3U)continue;return ESP_ERR_TIMEOUT;}
        if(got<=0)return ESP_FAIL;
        total+=(size_t)got;
    }
    body[total]='\0';return ESP_OK;
}
static void random_text(char *output,size_t length){
    static const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";const uint32_t count=sizeof(alphabet)-1U,cutoff=UINT32_MAX-(UINT32_MAX%count);
    for(size_t i=0;i<length;++i){uint32_t value;do value=esp_random();while(value>=cutoff);output[i]=alphabet[value%count];}output[length]='\0';
}
static void random_digits(char *output,size_t length){
    const uint32_t cutoff=UINT32_MAX-(UINT32_MAX%10U);
    for(size_t i=0;i<length;++i){uint32_t value;do value=esp_random();while(value>=cutoff);output[i]=(char)('0'+value%10U);}output[length]='\0';
}
static bool authorized(httpd_req_t *request){
    char token[40]={0};bool active;
    taskENTER_CRITICAL(&s_status_lock);active=s_status.authorized;taskEXIT_CRITICAL(&s_status_lock);
    if(!active||httpd_req_get_hdr_value_str(request,"X-Session-Token",token,sizeof(token))!=ESP_OK)return false;
    uint8_t diff=0;for(size_t i=0;i<sizeof(s_token);++i)diff|=(uint8_t)(token[i]^s_token[i]);
    wipe(token,sizeof(token));if(diff==0){taskENTER_CRITICAL(&s_status_lock);s_activity_us=esp_timer_get_time();taskEXIT_CRITICAL(&s_status_lock);}return diff==0;
}
static esp_err_t index_get(httpd_req_t *request){headers(request);httpd_resp_set_type(request,"text/html; charset=utf-8");httpd_resp_set_hdr(request,"Content-Encoding","gzip");return httpd_resp_send(request,(const char*)web_index_html_gz_start,web_index_html_gz_end-web_index_html_gz_start);}
static esp_err_t logo_get(httpd_req_t *request){headers(request);httpd_resp_set_type(request,"image/png");return httpd_resp_send(request,(const char*)web_logo_png_start,web_logo_png_end-web_logo_png_start);}
static esp_err_t fallback_get(httpd_req_t *request){headers(request);httpd_resp_set_status(request,"302 Found");httpd_resp_set_hdr(request,"Location","http://192.168.4.1/");return httpd_resp_send(request,NULL,0);}
static esp_err_t connect_post(httpd_req_t *request){
    bool first=false;taskENTER_CRITICAL(&s_status_lock);s_status.client_connected=true;s_status.seconds_remaining=0U;first=!s_browser_announced&&!s_status.authorized;s_browser_announced=true;taskEXIT_CRITICAL(&s_status_lock);
    taskENTER_CRITICAL(&s_status_lock);s_activity_us=esp_timer_get_time();taskEXIT_CRITICAL(&s_status_lock);if(first)event_send(WEB_EVENT_BROWSER_CONNECTED,NULL);return json(request,"{\"status\":\"pending-device-confirmation\"}");
}
static esp_err_t session_get(httpd_req_t *request){
    bool active;taskENTER_CRITICAL(&s_status_lock);active=s_status.authorized;taskEXIT_CRITICAL(&s_status_lock);
    char response[96];if(active)snprintf(response,sizeof(response),"{\"status\":\"authorized\",\"token\":\"%s\"}",s_token);else snprintf(response,sizeof(response),"{\"status\":\"pending-device-confirmation\"}");return json(request,response);
}
static esp_err_t disconnect_post(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    esp_err_t response=json(request,"{\"status\":\"closing\"}");
    event_send(WEB_EVENT_DISCONNECTED,NULL);
    command_t command={.kind=CMD_STOP};
    (void)xQueueSend(s_commands,&command,0);
    return response;
}
static esp_err_t welcome_get(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    char english[APP_WELCOME_MAX_BYTES+1U];
    app_config_copy_welcome(english);
    cJSON *root=cJSON_CreateObject();
    if(!root){wipe(english,sizeof(english));return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");}
    cJSON_AddStringToObject(root,"message",english);
    wipe(english,sizeof(english));
    char *response=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!response)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");
    esp_err_t error=json(request,response);cJSON_free(response);return error;
}
static esp_err_t welcome_put(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    char raw[512];if(body_read(request,raw,sizeof(raw))!=ESP_OK)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid body");
    cJSON *root=cJSON_Parse(raw);wipe(raw,sizeof(raw));if(!root)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid json");
    char english[APP_WELCOME_MAX_BYTES+1U]={0};
    bool good=json_string(root,"message",english,sizeof(english),true);
    cJSON_Delete(root);
    esp_err_t save_error=good?app_config_save_welcome(english):ESP_ERR_INVALID_ARG;
    wipe(english,sizeof(english));
    if(save_error==ESP_ERR_INVALID_ARG)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid welcome text");
    if(save_error!=ESP_OK)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"welcome storage failed");
    return json(request,"{\"saved\":true}");
}
static esp_err_t display_get(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    app_display_settings_t settings;app_config_copy_display_settings(&settings);
    char response[128];snprintf(response,sizeof(response),"{\"autoLockSeconds\":%u,\"revealSeconds\":%u,\"keySoundEnabled\":%s}",(unsigned)settings.auto_lock_seconds,(unsigned)settings.reveal_seconds,settings.key_sound_enabled?"true":"false");
    return json(request,response);
}
static esp_err_t language_get(httpd_req_t *request){
    /* Language is non-sensitive and must be known before device approval so
     * the authorization UI matches the language selected on first boot. */
    return json(request,app_config_language()==APP_LANGUAGE_CHINESE?"{\"language\":\"zh-CN\"}":"{\"language\":\"en\"}");
}
static esp_err_t language_put(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    char raw[96];if(body_read(request,raw,sizeof(raw))!=ESP_OK)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid body");
    cJSON *root=cJSON_Parse(raw);wipe(raw,sizeof(raw));if(!root)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid json");
    cJSON *item=cJSON_GetObjectItemCaseSensitive(root,"language");
    bool good=cJSON_IsString(item)&&item->valuestring;
    app_language_t language=APP_LANGUAGE_ENGLISH;
    if(good&&strcmp(item->valuestring,"zh-CN")==0)language=APP_LANGUAGE_CHINESE;
    else if(good&&strcmp(item->valuestring,"en")==0)language=APP_LANGUAGE_ENGLISH;
    else good=false;
    cJSON_Delete(root);
    if(!good)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid language");
    if(app_config_save_language(language)!=ESP_OK)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"language storage failed");
    return json(request,"{\"saved\":true}");
}
static esp_err_t display_put(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    char raw[192];if(body_read(request,raw,sizeof(raw))!=ESP_OK)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid body");
    cJSON *root=cJSON_Parse(raw);wipe(raw,sizeof(raw));if(!root)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid json");
    cJSON *lock=cJSON_GetObjectItemCaseSensitive(root,"autoLockSeconds"),*reveal=cJSON_GetObjectItemCaseSensitive(root,"revealSeconds"),*sound=cJSON_GetObjectItemCaseSensitive(root,"keySoundEnabled");
    bool good=cJSON_IsNumber(lock)&&cJSON_IsNumber(reveal)&&cJSON_IsBool(sound);
    app_display_settings_t settings={.auto_lock_seconds=good?(uint16_t)lock->valuedouble:0U,.reveal_seconds=good?(uint8_t)reveal->valuedouble:0U,.key_sound_enabled=good&&cJSON_IsTrue(sound)};
    if(good)good=lock->valuedouble==(double)settings.auto_lock_seconds&&reveal->valuedouble==(double)settings.reveal_seconds;
    cJSON_Delete(root);if(good)good=app_config_request_display_settings(&settings);
    if(!good)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid display settings");
    httpd_resp_set_status(request,"202 Accepted");return json(request,"{\"accepted\":true}");
}
static esp_err_t accounts_get(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    cJSON *root=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(root,"accounts");
    for(size_t i=0;i<vault_store_count();++i){vault_account_t account;if(!vault_store_get(i,&account))continue;cJSON *item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"index",i);cJSON_AddStringToObject(item,"platform",account.platform);cJSON_AddStringToObject(item,"url",account.url);cJSON_AddStringToObject(item,"username",account.username);cJSON_AddStringToObject(item,"password",account.password);cJSON_AddStringToObject(item,"note",account.note);cJSON_AddItemToArray(items,item);wipe(&account,sizeof(account));}
    char *response=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!response)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");esp_err_t error=json(request,response);cJSON_free(response);return error;
}
static bool json_string(cJSON *root,const char *name,char *output,size_t capacity,bool required){
    cJSON *item=cJSON_GetObjectItemCaseSensitive(root,name);if(!cJSON_IsString(item)||!item->valuestring)return !required;size_t length=strlen(item->valuestring);if(length>=capacity)return false;memcpy(output,item->valuestring,length+1U);return true;
}
static esp_err_t mutation_post(httpd_req_t *request){
    if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");
    char raw[1024];if(body_read(request,raw,sizeof(raw))!=ESP_OK)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid body");
    cJSON *root=cJSON_Parse(raw);wipe(raw,sizeof(raw));if(!root)return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid json");
    cJSON *op=cJSON_GetObjectItemCaseSensitive(root,"operation"),*index=cJSON_GetObjectItemCaseSensitive(root,"index");mutation_t mutation={0};
    if(cJSON_IsString(op)&&strcmp(op->valuestring,"add")==0)mutation.kind=MUT_ADD;else if(cJSON_IsString(op)&&strcmp(op->valuestring,"edit")==0)mutation.kind=MUT_EDIT;else if(cJSON_IsString(op)&&strcmp(op->valuestring,"delete")==0)mutation.kind=MUT_DELETE;
    if(cJSON_IsNumber(index)&&index->valuedouble>=0)mutation.index=(size_t)index->valuedouble;
    bool good=mutation.kind!=MUT_NONE;if(mutation.kind!=MUT_DELETE)good=good&&json_string(root,"platform",mutation.account.platform,sizeof(mutation.account.platform),true)&&json_string(root,"url",mutation.account.url,sizeof(mutation.account.url),false)&&json_string(root,"username",mutation.account.username,sizeof(mutation.account.username),false)&&json_string(root,"password",mutation.account.password,sizeof(mutation.account.password),true)&&json_string(root,"note",mutation.account.note,sizeof(mutation.account.note),false);
    cJSON_Delete(root);
    if(mutation.kind==MUT_ADD)good=good&&vault_store_count()<VAULT_MAX_ACCOUNTS;
    else good=good&&mutation.index<vault_store_count();
    if(good&&mutation.kind==MUT_DELETE)good=vault_store_get(mutation.index,&mutation.account);
    if(!good){wipe(&mutation,sizeof(mutation));return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"invalid fields");}
    mutation.id=esp_random();
    xSemaphoreTake(s_mutation_lock,portMAX_DELAY);
    if(s_pending.kind!=MUT_NONE){
        xSemaphoreGive(s_mutation_lock);wipe(&mutation,sizeof(mutation));
        httpd_resp_set_status(request,"409 Conflict");return json(request,"{\"error\":\"approval pending\"}");
    }
    s_pending=mutation;s_result="pending";
    xSemaphoreGive(s_mutation_lock);
    event_send(mutation.kind==MUT_ADD?WEB_EVENT_MUTATION_ADD:mutation.kind==MUT_EDIT?WEB_EVENT_MUTATION_EDIT:WEB_EVENT_MUTATION_DELETE,mutation.account.platform);
    char response[64];snprintf(response,sizeof(response),"{\"requestId\":%lu,\"status\":\"pending\"}",(unsigned long)mutation.id);httpd_resp_set_status(request,"202 Accepted");return json(request,response);
}
static esp_err_t mutation_status_get(httpd_req_t *request){if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");xSemaphoreTake(s_mutation_lock,portMAX_DELAY);const char *result=s_result;char response[64];snprintf(response,sizeof(response),"{\"status\":\"%s\"}",result);xSemaphoreGive(s_mutation_lock);return json(request,response);}
static esp_err_t change_pin_post(httpd_req_t *request){if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");event_send(WEB_EVENT_CHANGE_PIN,NULL);return json(request,"{\"accepted\":true}");}
static esp_err_t clear_data_post(httpd_req_t *request){if(!authorized(request))return httpd_resp_send_err(request,HTTPD_401_UNAUTHORIZED,"device authorization required");event_send(WEB_EVENT_CLEAR_DATA,NULL);return json(request,"{\"accepted\":true}");}
static esp_err_t server_start(void){
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.max_open_sockets=4;config.max_uri_handlers=20;config.max_req_hdr_len=4096;config.recv_wait_timeout=10;config.send_wait_timeout=10;config.lru_purge_enable=true;config.stack_size=7168;config.uri_match_fn=httpd_uri_match_wildcard;
    esp_err_t error=httpd_start(&s_server,&config);if(error!=ESP_OK)return error;
    const httpd_uri_t routes[]={{.uri="/",.method=HTTP_GET,.handler=index_get},{.uri="/assets/cipherport-logo.png",.method=HTTP_GET,.handler=logo_get},{.uri="/api/session/connect",.method=HTTP_POST,.handler=connect_post},{.uri="/api/session",.method=HTTP_GET,.handler=session_get},{.uri="/api/session/disconnect",.method=HTTP_POST,.handler=disconnect_post},{.uri="/api/settings/welcome",.method=HTTP_GET,.handler=welcome_get},{.uri="/api/settings/welcome",.method=HTTP_PUT,.handler=welcome_put},{.uri="/api/settings/display",.method=HTTP_GET,.handler=display_get},{.uri="/api/settings/display",.method=HTTP_PUT,.handler=display_put},{.uri="/api/settings/language",.method=HTTP_GET,.handler=language_get},{.uri="/api/settings/language",.method=HTTP_PUT,.handler=language_put},{.uri="/api/accounts",.method=HTTP_GET,.handler=accounts_get},{.uri="/api/mutations",.method=HTTP_POST,.handler=mutation_post},{.uri="/api/mutations/status",.method=HTTP_GET,.handler=mutation_status_get},{.uri="/api/actions/change-pin",.method=HTTP_POST,.handler=change_pin_post},{.uri="/api/actions/clear-data",.method=HTTP_POST,.handler=clear_data_post},{.uri="/*",.method=HTTP_GET,.handler=fallback_get}};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i){
        if(httpd_register_uri_handler(s_server,&routes[i])!=ESP_OK){httpd_stop(s_server);s_server=NULL;return ESP_FAIL;}
    }
    return ESP_OK;
}
static esp_err_t services_start(void){
    char password[9];s_browser_announced=false;random_digits(password,8);s_ap=esp_netif_create_default_wifi_ap();if(!s_ap)return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();esp_err_t error=esp_wifi_init(&init);if(error!=ESP_OK)return error;s_wifi=true;if((error=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK)return error;
    wifi_config_t config={0};snprintf((char*)config.ap.ssid,sizeof(config.ap.ssid),"CIPHERPORT");snprintf((char*)config.ap.password,sizeof(config.ap.password),"%s",password);config.ap.ssid_len=strlen((char*)config.ap.ssid);config.ap.channel=1;config.ap.max_connection=1;config.ap.authmode=WIFI_AUTH_WPA2_PSK;config.ap.pmf_cfg.required=true;
    if((error=esp_wifi_set_mode(WIFI_MODE_AP))==ESP_OK)error=esp_wifi_set_config(WIFI_IF_AP,&config);
    if(error==ESP_OK)error=esp_wifi_start();
    if(error==ESP_OK)error=server_start();
    if(error!=ESP_OK)return error;
    s_started_us=esp_timer_get_time();taskENTER_CRITICAL(&s_status_lock);s_activity_us=s_started_us;s_status.state=WEB_MANAGER_ACTIVE;snprintf(s_status.ssid,sizeof(s_status.ssid),"%s",config.ap.ssid);snprintf(s_status.password,sizeof(s_status.password),"%s",password);snprintf(s_status.address,sizeof(s_status.address),"http://192.168.4.1");s_status.seconds_remaining=WEB_MANAGER_CONNECT_SECONDS;taskEXIT_CRITICAL(&s_status_lock);return ESP_OK;
}
static void services_stop(void){
    if(s_server){httpd_stop(s_server);s_server=NULL;}if(s_wifi){esp_wifi_stop();esp_wifi_deinit();s_wifi=false;}if(s_ap){esp_netif_destroy_default_wifi(s_ap);s_ap=NULL;}
    xSemaphoreTake(s_mutation_lock,portMAX_DELAY);wipe(&s_pending,sizeof(s_pending));s_result="none";xSemaphoreGive(s_mutation_lock);wipe(s_token,sizeof(s_token));s_browser_announced=false;taskENTER_CRITICAL(&s_status_lock);memset(&s_status,0,sizeof(s_status));s_status.state=WEB_MANAGER_OFF;taskEXIT_CRITICAL(&s_status_lock);
}
static void mutation_finish(bool approve){
    mutation_t mutation;
    xSemaphoreTake(s_mutation_lock,portMAX_DELAY);mutation=s_pending;xSemaphoreGive(s_mutation_lock);
    if(mutation.kind==MUT_NONE)return;
    esp_err_t error=ESP_ERR_INVALID_STATE;
    if(approve){if(mutation.kind==MUT_ADD)error=vault_store_add(&mutation.account);else if(mutation.kind==MUT_EDIT)error=vault_store_update(mutation.index,&mutation.account);else error=vault_store_delete(mutation.index);}
    xSemaphoreTake(s_mutation_lock,portMAX_DELAY);s_result=!approve?"denied":error==ESP_OK?"approved":"failed";wipe(&s_pending,sizeof(s_pending));xSemaphoreGive(s_mutation_lock);
    if(approve)event_send(error==ESP_OK?WEB_EVENT_WRITE_OK:WEB_EVENT_WRITE_FAILED,mutation.account.platform);
    wipe(&mutation,sizeof(mutation));
}
static void network_task(void *argument){
    (void)argument;command_t command;for(;;){if(xQueueReceive(s_commands,&command,pdMS_TO_TICKS(100))==pdTRUE){if(command.kind==CMD_STOP){services_stop();continue;}if(command.kind==CMD_START){taskENTER_CRITICAL(&s_status_lock);s_status.state=WEB_MANAGER_STARTING;taskEXIT_CRITICAL(&s_status_lock);if(services_start()!=ESP_OK){services_stop();taskENTER_CRITICAL(&s_status_lock);s_status.state=WEB_MANAGER_FAILED;taskEXIT_CRITICAL(&s_status_lock);}}else if(command.kind==CMD_APPROVE)mutation_finish(true);else if(command.kind==CMD_DENY)mutation_finish(false);}
        if(s_status.state==WEB_MANAGER_ACTIVE){bool authorized,connected;int64_t activity_us;taskENTER_CRITICAL(&s_status_lock);authorized=s_status.authorized;connected=s_status.client_connected;activity_us=s_activity_us;taskEXIT_CRITICAL(&s_status_lock);if(authorized){int64_t now=esp_timer_get_time();bool expired=now-activity_us>=(int64_t)WEB_MANAGER_IDLE_SECONDS*1000000LL;taskENTER_CRITICAL(&s_status_lock);s_status.seconds_remaining=0U;taskEXIT_CRITICAL(&s_status_lock);if(expired){event_send(WEB_EVENT_EXPIRED,NULL);services_stop();}}else if(connected){taskENTER_CRITICAL(&s_status_lock);s_status.seconds_remaining=0U;taskEXIT_CRITICAL(&s_status_lock);}else{int64_t now=esp_timer_get_time();int64_t remaining=(int64_t)WEB_MANAGER_CONNECT_SECONDS*1000000LL-(now-s_started_us);bool expired=remaining<=0;taskENTER_CRITICAL(&s_status_lock);s_status.seconds_remaining=remaining>0?(unsigned)((remaining+999999LL)/1000000LL):0U;taskEXIT_CRITICAL(&s_status_lock);if(expired){event_send(WEB_EVENT_EXPIRED,NULL);services_stop();}}}}
}
esp_err_t web_manager_init(void){esp_err_t error=esp_netif_init();if(error!=ESP_OK)return error;error=esp_event_loop_create_default();if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return error;s_commands=xQueueCreate(4,sizeof(command_t));s_events=xQueueCreate(8,sizeof(web_manager_event_t));s_mutation_lock=xSemaphoreCreateMutex();if(!s_commands||!s_events||!s_mutation_lock)return ESP_ERR_NO_MEM;error=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,wifi_event,NULL);if(error!=ESP_OK)return error;error=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,wifi_event,NULL);if(error!=ESP_OK)return error;return xTaskCreate(network_task,"web_manager",8192,NULL,4,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
static bool request(command_kind_t kind){command_t command={.kind=kind};return s_commands&&xQueueSend(s_commands,&command,0)==pdTRUE;}
bool web_manager_request_start(void){return request(CMD_START);}
bool web_manager_request_stop(void){
    if(!request(CMD_STOP))return false;
    taskENTER_CRITICAL(&s_status_lock);
    if(s_status.state!=WEB_MANAGER_OFF)s_status.state=WEB_MANAGER_STOPPING;
    s_status.authorized=false;
    s_status.seconds_remaining=0U;
    taskEXIT_CRITICAL(&s_status_lock);
    return true;
}
bool web_manager_resolve_mutation(bool approve){return request(approve?CMD_APPROVE:CMD_DENY);}
bool web_manager_poll_event(web_manager_event_t *event){return event&&s_events&&xQueueReceive(s_events,event,0)==pdTRUE;}
void web_manager_set_authorized(bool value){if(value)random_text(s_token,32);else wipe(s_token,sizeof(s_token));taskENTER_CRITICAL(&s_status_lock);s_status.authorized=value;if(value)s_status.seconds_remaining=0U;s_activity_us=esp_timer_get_time();taskEXIT_CRITICAL(&s_status_lock);}
void web_manager_get_status(web_manager_status_t *status){if(!status)return;taskENTER_CRITICAL(&s_status_lock);*status=s_status;taskEXIT_CRITICAL(&s_status_lock);}
int64_t web_manager_last_activity_us(void){int64_t value;taskENTER_CRITICAL(&s_status_lock);value=s_activity_us;taskEXIT_CRITICAL(&s_status_lock);return value;}
