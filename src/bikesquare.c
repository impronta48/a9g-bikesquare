#include <string.h>
#include <stdio.h>
#include <api_os.h>
#include <api_event.h>
#include <api_socket.h>
#include <api_network.h>
#include <api_debug.h>
#include "api_info.h"
#include <api_gps.h>
#include <api_hal_uart.h>
#include "gps_parse.h"
#include "gps.h"
#include "time.h"
#include "api_hal_gpio.h"
#include "api_hal_pm.h"
#include "api_hal_adc.h"
#include "api_fota.h"
#include "api_hal_watchdog.h"
//test

/*******************************************************************/
/////////////////////////socket configuration////////////////////////

//#define DNS_DOMAIN  "demo.bikesquare.eu"
#define DNS_DOMAIN  "gps.bikesquare.eu"

#define SERVER_PORT 1337
// #define DNS_DOMAIN  "spillohome.asuscomm.com"
// #define SERVER_PORT 12346

#define RECEIVE_BUFFER_MAX_LENGTH 200
/*******************************************************************/


#define MAIN_TASK_STACK_SIZE    (2048 * 2)
#define MAIN_TASK_PRIORITY      0
#define MAIN_TASK_NAME          "MainTask_Task"

#define TEST_TASK_STACK_SIZE    (2048 * 2)
#define TEST_TASK_PRIORITY      1
#define TEST_TASK_NAME          "ConnectAndWrite_Task"

#define GPS_TASK_STACK_SIZE    (2048 * 2)
#define GPS_TASK_PRIORITY      0
#define GPS_TASK_NAME          "GPS Task"

#define ENGINE_TASK_STACK_SIZE    (2048 * 2)
#define ENGINE_TASK_PRIORITY      0
#define ENGINE_TASK_NAME          "ENGINE Test Task"

#define TIMER_TASK_STACK_SIZE    (1024)
#define TIMER_TASK_PRIORITY      0
#define TIMER_TASK_NAME          "Timer Task"

#define SOFT_VERSION          "000"
#define FOTA_HTTP_SERVER      "http://spillohome.asuscomm.com:9811/fota/A9G_%s_FW_tonew.pack"

#define GPS_REFRESH_TIME 2500
#define WRITE_INFO_TIME 30000
#define DURATA_WATCHDOG 300   // in secondi, alla scadenza resetta il device. Viene riarmato quando riesce a inviare qualcosa al server
#define INTERVALLO_METRONOMO 1000 // timer per contare quanto manca allo scadere del watchdog

void SetBikeLockLocked();
void SetBikeLockUnLocked();
void updateFOTA();
int getNumber(char []);


bool networkFlag = false;

int ct_secondiMetronomo = 0;

static HANDLE socketTaskHandle = NULL;
static HANDLE timerTask = NULL;

int socketFd = -1;
uint8_t buffer[RECEIVE_BUFFER_MAX_LENGTH];
HANDLE sem = NULL;
int errorCode = 0;

bool isGpsOn = true;

double latitude = 0;
double longitude = 0;

uint8_t IMEI[16];



GPS_Info_t* gpsInfo;

GPIO_config_t socketLed = {
    .mode         = GPIO_MODE_OUTPUT,
    .pin          = GPIO_PIN30,
    .defaultLevel = GPIO_LEVEL_LOW
};

GPIO_config_t gpsLed = {
    .mode         = GPIO_MODE_OUTPUT,
    .pin          = GPIO_PIN25,
    .defaultLevel = GPIO_LEVEL_LOW
};

GPIO_config_t engine = {
    .mode         = GPIO_MODE_OUTPUT,
    .pin          = GPIO_PIN29,
    .defaultLevel = GPIO_LEVEL_LOW  //GPIO_LEVEL_HIGH
};

GPIO_config_t bikeLockPIN_A = {
    .mode         = GPIO_MODE_OUTPUT,
    .pin          = GPIO_PIN6,
    .defaultLevel = GPIO_LEVEL_LOW
};

GPIO_config_t bikeLockPIN_B = {
    .mode         = GPIO_MODE_OUTPUT,
    .pin          = GPIO_PIN7,
    .defaultLevel = GPIO_LEVEL_LOW
};


int engineStatus = 1;

bool sendVersion=false;

uint8_t isFixed=0;

int writeCount=0;

char* strMyTok(char** newString, char* delimiter)
{
    char* string = *newString;
    char* delimiterFound = (char*) 0;
    int tokLenght = 0;
    char* tok = (char*) 0;

    if(!string) return (char*) 0;

    delimiterFound = strstr(string, delimiter);

    if(delimiterFound){
        tokLenght = delimiterFound-string;
    }else{
        tokLenght = strlen(string);
    }

    tok = malloc(tokLenght + 1);
    memcpy(tok, string, tokLenght);
    tok[tokLenght] = '\0';

    *newString = delimiterFound ? delimiterFound + strlen(delimiter) : (char*)0;

    return tok;
}


void EventDispatch(API_Event_t* pEvent)
{
    const char FNAME[] = "EventDispatch->";
    switch(pEvent->id)
    {
        case API_EVENT_ID_NO_SIMCARD:
            Trace(10,"%s!!NO SIM CARD%d!!!!",FNAME,pEvent->param1);
            networkFlag = false;
            break;
        case API_EVENT_ID_NETWORK_REGISTER_SEARCHING:
            Trace(2,"%snetwork register searching",FNAME);
            networkFlag = false;
        case API_EVENT_ID_NETWORK_REGISTERED_HOME:
        case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
            Trace(2,"%snetwork register success",FNAME);
            Network_StartAttach();
            break;

        case API_EVENT_ID_NETWORK_ATTACHED:
            Trace(2,"%snetwork attach success",FNAME);
            Network_PDP_Context_t context = {
                .apn        ="iot.1nce.net",
                .userName   = ""    ,
                .userPasswd = ""
            };
            Network_StartActive(context);
            break;

        case API_EVENT_ID_NETWORK_ACTIVATED:
            Trace(2,"%snetwork activate success",FNAME);
            networkFlag = true;
            //OS_ReleaseSempaphore(semGPRS);
            // if(sem)
            //     OS_ReleaseSemaphore(sem);
            sem = 1;
            break;

        case API_EVENT_ID_SOCKET_CONNECTED:
            Trace(2,"%sevent connect",FNAME);
            GPIO_SetLevel(socketLed, GPIO_LEVEL_HIGH);
            // if(sem)
            //     OS_ReleaseSemaphore(sem);
            sem = 1;
            break;

        case API_EVENT_ID_SOCKET_SENT:
            Trace(2,"%ssocket sent",FNAME);
            // if(sem)
            //     OS_ReleaseSemaphore(sem);
            sem = 1;
            break;
        case API_EVENT_ID_SOCKET_RECEIVED:
        {
            int fd = pEvent->param1;
            int length = pEvent->param2>RECEIVE_BUFFER_MAX_LENGTH?RECEIVE_BUFFER_MAX_LENGTH:pEvent->param2;
            memset(buffer,0,sizeof(buffer));
            length = Socket_TcpipRead(fd,buffer,length);
            Trace(2,"%ssocket %d received %d bytes data:%s",FNAME,fd,length,buffer);
            char rsp[20];
            sprintf(rsp, "%s", buffer);
            Trace(2, rsp);





            char motore[10];
            char versione[10];
            char cmdLucchetto[610];
            //char rsp[20]="[off|v|unlock]";


            char *sptr, *lptr;
            int len;
            lptr = rsp;                                   // start of search
            lptr++;
            sptr = strchr(lptr, '|');
            if (sptr != NULL){
                len = sptr - lptr;                          // length of phrase
                //printf(" lptr :'%s\n sptr :'%s\n len :'%d'\n",lptr,sptr,len);

                if (len >0 ){
                    memcpy(motore, lptr, len);               // copy the substring
                    motore[len]= '\0';
                }

                lptr = sptr;
                lptr++;
                sptr = strchr(lptr, '|');
                len = sptr - lptr;                          // length of phrase
                //printf(" lptr :'%s\n sptr :'%s\n len :'%d'\n",lptr,sptr,len);
                versione[len]= '\0';
                if (len >0 ){
                    memcpy(versione, lptr, len);               // copy the substring
                    versione[len]= '\0';
                }


                lptr = sptr;
                lptr++;
                sptr = strchr(lptr, ']');
                len = sptr - lptr;                          // length of phrase
                //printf(" lptr :'%s\n sptr :'%s\n len :'%d'\n",lptr,sptr,len);

                if (len >0 ){
                    memcpy(cmdLucchetto, lptr, len);               // copy the substring
                    cmdLucchetto[len]= '\0';
                }

                Trace(2, "%s motore :'%s' versione :'%s' lucchetto :'%s'",FNAME,motore,versione,cmdLucchetto);
                //printf("%s motore :'%s' versione :'%s' lucchetto :'%s'\n",motore,versione,cmdLucchetto);
            }




            if(!strcmp(motore, "on")){
                Trace(2, "%son command received",FNAME);
                //GPIO_SetLevel(engine, GPIO_LEVEL_HIGH);
                GPIO_SetLevel(engine, GPIO_LEVEL_LOW);
                engineStatus=1;
                // temporary drive also the lock pin -> SetBikeLockUnLocked
                //Trace(2, "%sTEST ONLY-->>, calling SetBikeLockUnLocked (Aperto also when receiving [on] command",FNAME);
                //SetBikeLockUnLocked();
            }else if(!strcmp(motore, "off")){
                Trace(2, "%soff command received",FNAME);
                //GPIO_SetLevel(engine, GPIO_LEVEL_LOW);
                GPIO_SetLevel(engine, GPIO_LEVEL_HIGH);
                engineStatus=0;

                // temporary drive also the lock pin -> SetBikeLockLocked
                //Trace(2, "%sTEST ONLY-->>, calling SetBikeLockLocked (Chiuso) also when receiving [off] command",FNAME);
                //SetBikeLockLocked();
            }

            if(!strcmp(rsp, "[V?]")){
                sendVersion=true;
            }else if(rsp[1]=='V'){
                int verNumber = getNumber(rsp);
                if(atoi(SOFT_VERSION)<verNumber) {
                    updateFOTA();
                }
            }

            if(!strcmp(cmdLucchetto, "lock")){
                Trace(2, "%slock command received",FNAME);
                Trace(2, "%scalling SetBikeLockLocked ",FNAME);
                SetBikeLockLocked();
            } else if(!strcmp(cmdLucchetto, "unlock")){
                Trace(2, "%son command received",FNAME);
                Trace(2, "%scalling SetBikeLockUnLocked ",FNAME);
                SetBikeLockUnLocked();
            }
            break;
        }
        case API_EVENT_ID_SOCKET_CLOSED:
        {
            int fd = pEvent->param1;
            Trace(2,"%ssocket %d closed",FNAME,fd);
            GPIO_SetLevel(socketLed, GPIO_LEVEL_LOW);
            // if(sem)
            //     OS_ReleaseSemaphore(sem);
            sem = 1;
            break;
        }
        case API_EVENT_ID_SOCKET_ERROR:
        {
            int fd = pEvent->param1;
            Trace(2,"%ssocket %d error occurred,cause:%d",FNAME,fd,pEvent->param2);
            errorCode = pEvent->param2;
            // if(sem)
            //     OS_ReleaseSemaphore(sem);
            sem = 1;
            break;
        }
        case API_EVENT_ID_GPS_UART_RECEIVED:
            // Trace(1,"received GPS data,length:%d, data:%s,flag:%d",pEvent->param1,pEvent->pParam1,flag);
            GPS_Update(pEvent->pParam1,pEvent->param1);
            break;
        case API_EVENT_ID_UART_RECEIVED:
            if(pEvent->param1 == UART1)
            {
                uint8_t data[pEvent->param2+1];
                data[pEvent->param2] = 0;
                memcpy(data,pEvent->pParam1,pEvent->param2);
                Trace(1,"%suart received data,length:%d,data:%s",FNAME,pEvent->param2,data);
                if(strcmp(data,"close") == 0)
                {
                    Trace(1,"%sclose gps",FNAME);
                    GPS_Close();
                    isGpsOn = false;
                }
                else if(strcmp(data,"open") == 0)
                {
                    Trace(1,"%sopen gps",FNAME);
                    GPS_Open(NULL);
                    isGpsOn = true;
                }
            }
            break;
        default:
            break;
    }
}




static void processFotaUpgradeData(const unsigned char *data, int len)
{
    const char FNAME[] = "processFotaUpgradeData->";
    if(len)
    {
        MEMBLOCK_Trace(1, (uint8_t*)data, (uint16_t)len, 16);
        Trace(1,"%sfota total len:%d data:%s",FNAME, len, data);
        if(!API_FotaInit(len))
            goto upgrade_faile;
        if(API_FotaReceiveData((unsigned char*)data, (int)len) == 0);
    }else{//error
        Trace(1,"%sfota total len:%d data:%s",FNAME, len, data);
        goto upgrade_faile;
    }
    return;

upgrade_faile:
    Trace(1,"%sserver fota false",FNAME);
    API_FotaClean();
}

void SetBikeLockUnLocked()
{
    const char FNAME[] = "SetBikeLockUnLocked->";
    Trace(1,"%sLucchetto Aperto",FNAME);

    Trace(1,"%sset bikeLockPIN_B GPIO_LEVEL_HIGH",FNAME);

    GPIO_SetLevel(bikeLockPIN_B, GPIO_LEVEL_HIGH);
    OS_Sleep(1000);
    Trace(1,"%sset bikeLockPIN_B GPIO_LEVEL_LOW",FNAME);
    GPIO_SetLevel(bikeLockPIN_B, GPIO_LEVEL_LOW);

}

void SetBikeLockLocked()
{
    const char FNAME[] = "SetBikeLockLocked->";
    Trace(1,"%sLucchetto Chiuso",FNAME);

    Trace(1,"%sset bikeLockPIN_A GPIO_LEVEL_HIGH",FNAME);

    GPIO_SetLevel(bikeLockPIN_A, GPIO_LEVEL_HIGH);
    OS_Sleep(1000);
    Trace(1,"%sset bikeLockPIN_A GPIO_LEVEL_LOW",FNAME);
    GPIO_SetLevel(bikeLockPIN_A, GPIO_LEVEL_LOW);

}

void updateFOTA(){
    const char FNAME[] = "updateFOTA->";
    char url[256];
    memset(url, 0, sizeof(url));
    sprintf(url, FOTA_HTTP_SERVER, SOFT_VERSION);
    Trace(1,"%sfota network activate success url %s",FNAME, url);
    if(API_FotaByServer(url, processFotaUpgradeData) == 0)
        Trace(1,"%sfota wait",FNAME);
    else
        Trace(1,"%sfota fail",FNAME);
}

int getNumber(char a[]){
    char number[4];
    for(int i=2, n=0; i<strlen(a)-1; i++, n++){
        number[n] = a[i];
    }
    return atoi(number);
}

void CreateSem(HANDLE* sem_)
{
    const char FNAME[] = "CreateSem->";
    Trace(4,"%sCreateSem-> sem:%d",FNAME,*sem_);
    *sem_ = 0;
    // *sem = OS_CreateSemaphore(0);
}

void WaitSem(HANDLE* sem_)
{
    const char FNAME[] = "WaitSem->";
    // OS_WaitForSemaphore(*sem,OS_WAIT_FOREVER);
    // OS_DeleteSemaphore(*sem);
    // *sem = NULL;
    Trace(4,"%sWaitSem-> sem:%d",FNAME,*sem_);
    while(*sem_ == 0)
        OS_Sleep(1);
    *sem_ = 0;
}

bool Connect()
{
    const char FNAME[] = "Connect->";
    memset(buffer,0,sizeof(buffer));
    if(DNS_GetHostByName2(DNS_DOMAIN,(char*)buffer) != 0)
        return false;
    Trace(2,"%sDNS,domain:%s,ip:%s,strlen(ip):%d",FNAME,DNS_DOMAIN,buffer,strlen(buffer));
    CreateSem(&sem);
    socketFd = Socket_TcpipConnect(TCP,buffer,SERVER_PORT);
    Trace(2,"%sconnect tcp server,socketFd:%d",FNAME,socketFd);
    WaitSem(&sem);
    Trace(2,"%sconnect end",FNAME);
    if(errorCode != 0)
    {
        errorCode = 0;
        Trace(2,"%serror ocurred",FNAME);
        return false;
    }
    return true;
}
bool Write(uint8_t* data, uint16_t len)
{
    const char FNAME[] = "Write->";
    Trace(2,"%sWrite",FNAME);
    CreateSem(&sem);
    int ret = Socket_TcpipWrite(socketFd,data,len);
    if(ret <= 0)
    {
        Trace(2,"%ssocket write fail:%d",FNAME,ret);
        return false;
    }
    Trace(2,"%s### socket %d send %d bytes data to server:%s,ret:%d",FNAME,socketFd, len, data,ret);
    WaitSem(&sem);
    Trace(2,"%s### write end",FNAME);
    if(writeCount==5){
        sendVersion=true;
        writeCount=0;
    }else writeCount++;

    if(errorCode != 0)
    {
        errorCode = 0;
        Trace(2,"%serror ocurred",FNAME);
        return false;
    }
    return true;
}

bool Close()
{
    CreateSem(&sem);
    Socket_TcpipClose(socketFd);
    WaitSem(&sem);
    return true;
}

void ImeiRead()
{
    const char FNAME[] = "ImeiRead->";
    memset(IMEI,0,sizeof(IMEI));
    INFO_GetIMEI(IMEI);
    Trace(2, "%s%s",FNAME, IMEI);
}

void GPS_Task(void *pData)
{
    const char FNAME[] = "GPS_Task->";
    uint8_t buffer2[300];

    while(!networkFlag)
    {
        Trace(1,"%swait for gprs register complete",FNAME);

        OS_Sleep(2000);
    }

    gpsInfo = Gps_GetInfo();
    //open GPS hardware(UART2 open either)
    GPS_Init();
    GPS_Open(NULL);



    //wait for gps start up, or gps will not response command
    while(gpsInfo->rmc.latitude.value == 0)
        OS_Sleep(1000);


    // set gps nmea output interval
    for(uint8_t i = 0;i<5;++i)
    {
        bool ret = GPS_SetOutputInterval(10000);
        Trace(1, "%sset gps ret:%d",FNAME,ret);
        if(ret)
            break;
        OS_Sleep(1000);
    }

    if(!GPS_GetVersion(buffer2,150))
        Trace(1,"%sget gps firmware version fail",FNAME);
    else
        Trace(1,"%sgps firmware version:%s",FNAME,buffer2);

    if(!GPS_SetOutputInterval(1000))
        Trace(1,"%sset nmea output interval fail",FNAME);

    Trace(1,"%sinit ok",FNAME);

    while(1){
        if(isGpsOn)
        {
            //show fix info
            isFixed = gpsInfo->gsa[0].fix_type > gpsInfo->gsa[1].fix_type ?gpsInfo->gsa[0].fix_type:gpsInfo->gsa[1].fix_type;
            char* isFixedStr = "";
            if(isFixed == 2){
                isFixedStr = "2D fix";
                GPIO_SetLevel(gpsLed, GPIO_LEVEL_HIGH);
            }
            else if(isFixed == 3)
            {
                GPIO_SetLevel(gpsLed, GPIO_LEVEL_HIGH);
                if(gpsInfo->gga.fix_quality == 1)
                    isFixedStr = "3D fix";
                else if(gpsInfo->gga.fix_quality == 2)
                    isFixedStr = "3D/DGPS fix";
            }
            else{
                isFixedStr = "no fix";
                GPIO_SetLevel(gpsLed, GPIO_LEVEL_LOW);
            }


            //convert unit ddmm.mmmm to degree(°)
            int temp = (int)(gpsInfo->rmc.latitude.value/gpsInfo->rmc.latitude.scale/100);
            latitude = temp+(double)(gpsInfo->rmc.latitude.value - temp*gpsInfo->rmc.latitude.scale*100)/gpsInfo->rmc.latitude.scale/60.0;
            temp = (int)(gpsInfo->rmc.longitude.value/gpsInfo->rmc.longitude.scale/100);
            longitude = temp+(double)(gpsInfo->rmc.longitude.value - temp*gpsInfo->rmc.longitude.scale*100)/gpsInfo->rmc.longitude.scale/60.0;


            //you can copy ` latitude,longitude ` to http://www.gpsspg.com/maps.htm check location on map

            snprintf(buffer2,sizeof(buffer2),"GPS fix mode:%d, BDS fix mode:%d, fix quality:%d, satellites tracked:%d, gps sates total:%d, is fixed:%s, coordinate:WGS84, Latitude:%f, Longitude:%f, unit:degree,altitude:%f",gpsInfo->gsa[0].fix_type, gpsInfo->gsa[1].fix_type,
                                                                gpsInfo->gga.fix_quality,gpsInfo->gga.satellites_tracked, gpsInfo->gsv[0].total_sats, isFixedStr, latitude,longitude,gpsInfo->gga.altitude);

            //show in tracer
            Trace(3,"%s GPSBuffer:%s",FNAME,buffer2);
            //send to UART1
            //UART_Write(UART1,buffer,strlen(buffer));
            //UART_Write(UART1,"\r\n\r\n",4);
        }
        Trace(1,"%spreparo a sleep",FNAME);
        PM_SleepMode(true);
        OS_Sleep(GPS_REFRESH_TIME);
        Trace(1,"%sfine sleep",FNAME);
        PM_SleepMode(false);
    }
}

void ConnectAndWrite_Task(void* param)
{
    const char FNAME[] = "ConnectAndWrite_Task->";
    uint16_t batteryLevel = 0, battery_mV = 0;
    ADC_Config_t config = {
        .channel = ADC_CHANNEL_0,
        .samplePeriod = ADC_SAMPLE_PERIOD_100MS
    };
    ADC_Init(config);
    int failCount = 0;
    int count = 0;
    ImeiRead();
    char to_send_string[300];
    char date[9];
    char time[9];
    //char recived_string[300];
    WaitSem(&sem);
    Trace(2,"%ssem:%d,%p",FNAME,(int)sem,(void*)sem);
    Trace(1,"%sstart connect now",FNAME);
    Connect();
    while(1)
    {
            if(failCount == 5)
            {
                Close();
            }
            if(failCount >= 5)
            {
                if(Connect())
                    failCount = 0;
                else
                    ++failCount;
            }
            else
            {
                //if(isFixed==2 || isFixed==3){
                    if(ADC_Read(ADC_CHANNEL_0, &batteryLevel, &battery_mV))
                        Trace(2,"%sADC value:%d, %dmV",FNAME, batteryLevel, battery_mV);
                    sprintf(date, "%02d/%02d/%02d", gpsInfo->rmc.date.year, gpsInfo->rmc.date.month, gpsInfo->rmc.date.day);
                    sprintf(time, "%02d:%02d:%02d", gpsInfo->rmc.time.hours,gpsInfo->rmc.time.minutes,gpsInfo->rmc.time.seconds);
                    Trace(2,"%sCheck GPS fixed mode",FNAME);
                    if(isFixed==2 || isFixed==3) {
                        Trace(2,"%sFixed 2 or 3, preparing real coordinates",FNAME);
                        sprintf(to_send_string, "bnmm;%s;%f;%f;%s,%s+%02d;%05d;%d", IMEI, latitude, longitude, date, time, gpsInfo->zda.hour_offset, batteryLevel, engineStatus);
                        }
                    else {
                        Trace(2,"%sNot yet Fixed, preparing 0,0 coordinates",FNAME);
                        sprintf(to_send_string, "bnmm;%s;%f;%f;%s,%s+%02d;%05d;%d", IMEI, 0.0, 0.0, date, time, gpsInfo->zda.hour_offset, batteryLevel, engineStatus);
                    }
                    if(sendVersion){
                        sprintf(to_send_string + strlen(to_send_string), ";%s", SOFT_VERSION);
                        sendVersion=false;
                    }
                    sprintf(to_send_string + strlen(to_send_string), "\r\n");
                    if(!Write(to_send_string, strlen(to_send_string)))
                    {
                        ++failCount;
                        Trace(2,"%swrite fail",FNAME);
                    }
                    else{
                        Trace(2,"%sRiarmo WatchDog",FNAME);
                        ct_secondiMetronomo = 0;
                        WatchDog_KeepAlive();
                    }
                //}
            }
            Trace(2,"%sLoop applicativo count:%d failcount:%d isFixed:%d sem:%d",FNAME,count++, failCount,isFixed, (int)sem);
        OS_Sleep(WRITE_INFO_TIME);
    }
}

void StartTimer1(uint32_t interval);

void OnTimer1(void* param)
{
    const char FNAME[] = "OnTimer1->";
    Trace(1,"%sMetronomo,tempo assoluto:%dms",FNAME,(uint32_t)(clock()/CLOCKS_PER_MSEC));
    Trace(1,"%sSecondi allo scadere del reset : %d",FNAME, DURATA_WATCHDOG - ct_secondiMetronomo);
    ct_secondiMetronomo++;
    StartTimer1(INTERVALLO_METRONOMO);
}

void StartTimer1(uint32_t interval)
{
    OS_StartCallbackTimer(timerTask,interval,OnTimer1,NULL);
}

void TimerTask(void* param)
{
    API_Event_t* event=NULL;
    while(1)
    {
        if(OS_WaitEvent(timerTask, (void**)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}


void MainTask_Task(void *pData)
{
    const char FNAME[] = "MainTask_Task->";

    API_Event_t* event=NULL;
    TIME_SetIsAutoUpdateRtcTime(true);


    OS_Sleep(3000);
    Trace(1,"%sDelay Iniziale precauzionale 3000",FNAME);

    Trace(1,"%sAvvio watchdog per reset in assenza invii validi durata in secondi: %d ",FNAME,DURATA_WATCHDOG);
    WatchDog_Open(WATCHDOG_SECOND_TO_TICK(DURATA_WATCHDOG));
    Trace(1,"%sAvvio METRONOMO ogni: %d secondi",FNAME,INTERVALLO_METRONOMO);
    StartTimer1(INTERVALLO_METRONOMO);


    // semGPRS = OS_CreateSemaphore(0);
    CreateSem(&sem);
    OS_CreateTask(ConnectAndWrite_Task,
        NULL, NULL, TEST_TASK_STACK_SIZE, TEST_TASK_PRIORITY, 0, 0, TEST_TASK_NAME);

    UART_Config_t config = {
        .baudRate = UART_BAUD_RATE_115200,
        .dataBits = UART_DATA_BITS_8,
        .stopBits = UART_STOP_BITS_1,
        .parity   = UART_PARITY_NONE,
        .rxCallback = NULL,
        .useEvent   = true
    };
    UART_Init(UART1,config);

    GPIO_Init(socketLed);
    GPIO_Init(gpsLed);
    GPIO_Init(engine);
    GPIO_Init(bikeLockPIN_A);
    GPIO_Init(bikeLockPIN_B);

    //Create UART1 send task and location print task
    OS_CreateTask(GPS_Task,
            NULL, NULL, GPS_TASK_STACK_SIZE, GPS_TASK_PRIORITY, 0, 0, GPS_TASK_NAME);



    while(1)
    {
        if(OS_WaitEvent(socketTaskHandle, (void**)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void bikesquare_Main(void)
{
    timerTask = OS_CreateTask(TimerTask,
    NULL, NULL, TIMER_TASK_STACK_SIZE, TIMER_TASK_PRIORITY, 0, 0, TIMER_TASK_NAME);

    socketTaskHandle = OS_CreateTask(MainTask_Task,
        NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);


    OS_SetUserMainHandle(&socketTaskHandle);
}

