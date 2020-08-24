#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/iosupport.h>
#include "services.h"
#include "minisoc.h"
#include "mythread.h"
#include "input.h"

#define MAX_SESSIONS 1
#define SERVICE_ENDPOINTS 3
#define CRASH *(u32*)__LINE__ = 0xFF;

static Result should_terminate(int *term_request) {
    u32 notid;

    Result ret = srvReceiveNotification(&notid);
    if (R_FAILED(ret)) {
        return ret;
    }
    if (notid == 0x100) {// term request
        *term_request = 1;
    }
    return 0;
}

// this is called before main
void __appInit() {
    srvSysInit();
    
    if(cfguInit() > 0)		
        CRASH;
    //consoleDebugInit(debugDevice_SVC);
    //gdbHioDevInit();
    //gdbHioDevRedirectStdStreams(false, true, false);
    if(miniSocInit() > 0)
        CRASH;
}

// this is called after main exits
void __appExit() {
    miniSocExit();
    cfguExit();
    srvSysExit();
}

// stubs for non-needed pre-main functions
void __sync_init();
void __sync_fini();
void __system_initSyscalls();
void __libc_init_array(void);
void __libc_fini_array(void);

void initSystem(void (*retAddr)(void)) {
    __libc_init_array();
    __sync_init();
    __system_initSyscalls();
    __appInit();
}

void __ctru_exit(int rc) {
    __appExit();
    __sync_fini();
    __libc_fini_array();
    svcExitProcess();
}

static u8 tag_state = 0;
static Handle events[2] = {-1, -1};
static u8 ALIGN(8) threadStack[0x1000] = {0};
static u8 ALIGN(8) statbuf[0x800] = {0};
static int sockfd = -1;
static int connfd = -1;
static struct sockaddr_in cli; 

typedef struct 
{
    u8 *buf;
    MyThread thread;
    MyThread hidthread;
    char done;
    char connected;
} sockThreadStruct;

void sockrwThread(void *arg)
{
    sockThreadStruct *data = (sockThreadStruct*) arg;
    Result ret = 0;
    struct pollfd fds[1];
    int nfds = 1;

    if(connfd == -1)
    {
        size_t len = sizeof(cli); 
        while(1)
        {
            if(data->done) break;
            memset(fds, 0, sizeof(fds));
            fds[0].fd = sockfd;
            fds[0].events = POLLIN;
            ret = socPoll(fds, nfds, 50);
            if(ret < 0) 
                MyThread_Exit();
            
            if(ret > 0)
            {
                if(fds[0].revents & POLLIN)
                {
                    connfd = socAccept(sockfd, &cli, &len); 
                    if(connfd < 0) 
                        CRASH;
                    if(events[0] != -1) svcSignalEvent(events[0]);
                    data->connected = true;
                    break;
                }
            }
        }
    }

    MyThread_Exit();
}

void hidThread()
{
    while(1)
    {
        u32 key = waitInput();
        if(key & BUTTON_START) 
        {
            if(events[1] != -1)
                svcSignalEvent(events[1]);
            tag_state = NFC_TagState_OutOfRange;
            break;
        }
    }

    MyThread_Exit();
}

void sockSendRecvData(sockThreadStruct *data, u32 *cmdbuf)
{
    if(!data->connected) return;
    memcpy(&data->buf[0], (u8*)&cmdbuf[0], 256);
    socSend(connfd, data->buf, 256, 0);
    socRecv(connfd, &data->buf[0], 256, 0);
    memcpy((u8*)&cmdbuf[0], &data->buf[0], 256);           
}

/*
char* GetCommandName(u16 cmdid)
{
    static char *cmdstr;

    switch (cmdid) {
		case 0x0001: cmdstr = "Initialize"; break;
		case 0x0002: cmdstr = "Shutdown"; break;
		case 0x0003: cmdstr = "StartCommunication"; break;
		case 0x0004: cmdstr = "StopCommunication"; break;
		case 0x0005: cmdstr = "StartTagScanning"; break;
		case 0x0006: cmdstr = "StopTagScanning"; break;
		case 0x0007: cmdstr = "LoadAmiiboData"; break;
		case 0x0008: cmdstr = "ResetTagScanState"; break;
		case 0x0009: cmdstr = "UpdateStoredAmiiboData"; break;
		case 0x000B: cmdstr = "GetTagInRangeEvent"; break;
		case 0x000C: cmdstr = "GetTagOutOfRangeEvent"; break;
		case 0x000D: cmdstr = "GetTagState"; break;
		case 0x000F: cmdstr = "CommunicationGetStatus"; break;
		case 0x0010: cmdstr = "GetTagInfo2"; break;
		case 0x0011: cmdstr = "GetTagInfo"; break;
		case 0x0012: cmdstr = "CommunicationGetResult"; break;
		case 0x0013: cmdstr = "OpenAppData"; break;
		case 0x0014: cmdstr = "InitializeWriteAppData"; break;
		case 0x0015: cmdstr = "ReadAppData"; break;
		case 0x0016: cmdstr = "WriteAppData"; break;
		case 0x0017: cmdstr = "GetAmiiboSettings"; break;
		case 0x0018: cmdstr = "GetAmiiboConfig"; break;
		case 0x0019: cmdstr = "GetAppDataInitStruct"; break;
        case 0x001A: cmdstr = "MountRomData"; break;
        case 0x001B: cmdstr = "GetAmiiboIdentificationBlock"; break;
		case 0x001F: cmdstr = "StartOtherTagScanning"; break;
		case 0x0020: cmdstr = "SendTagCommand"; break;
		case 0x0021: cmdstr = "Cmd21"; break;
		case 0x0022: cmdstr = "Cmd22"; break;
        case 0x401: cmdstr = "Reset"; break;
        case 0x402: cmdstr = "GetAppDataConfig"; break;
        case 0x407: cmdstr = "IsAppdatInited called"; break; 
		case 0x0404: cmdstr = "SetAmiiboSettings"; break;
        default:
            cmdstr = "Unknown Command called"; break;
	}
    return cmdstr;
}

int no_of_cmdbuf_to_dump(int cmdid)
{
    int n = 2;
    switch(cmdid)
    {
        case 0x5: n += 0; break;
        case 0x6: n += 0; break;
        case 0xD: n += 1; break;
        case 0x11: n += 12; break;
        case 0x7: n += 0; break;
        case 0x17: n+= 0x2B; break;
        case 0x18: n += 17; break;
        case 0x13: n += 0; break;
        case 0x15: n += (0xD8/4) + 1; break;
    }
    return n;
}

*/
void handle_commands(sockThreadStruct *data)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    u16 cmdid = cmdbuf[0] >> 16;
   // printf("Command %s (0x%x) called\n", GetCommandName(cmdid), cmdbuf[0]);
    //This is a bare-minimum ipc-handler for some critical funcs to ensure that stuff isn't broken when 
    //the companion isn't connected
    switch(cmdid)
    {
        case 1: //Initialize
        {	
            data->done = 0;
            MyThread_Create(&data->thread, &sockrwThread, (void*)data, threadStack, 0x1000, 15, -2);
            tag_state = NFC_TagState_ScanningStopped;
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 0);
            cmdbuf[1] = 0;
            break;
        }

        case 2: //Finalize
        {
            data->done = 1;
            socClose(sockfd);
            MyThread_Join(&data->thread, 2e+9);
            tag_state = NFC_TagState_Uninitialized;
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 0);
            cmdbuf[1] = 0;
            break;
        }

        case 3: // StartCommunication
        {
    //		printf("StartCommunication\n");
            // Now server is ready to listen and verification
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 0);
            cmdbuf[1] = 0;
            break;
        }

        case 4: // StopCommunication
        {
            //socClose(connfd);
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 0);
            cmdbuf[1] = 0;
            break;
        }

        case 5:
        {
            if(data->connected) svcSignalEvent(events[0]);
            tag_state = NFC_TagState_Scanning;
            sockSendRecvData(data, cmdbuf);
            break;
        }

        case 6:
        {
            if(data->hidthread.handle != -1) 
            {
                MyThread_Join(&data->hidthread, 1e+9);
                data->hidthread.handle = -1;
            }
            svcSignalEvent(events[1]);
            sockSendRecvData(data, cmdbuf);
            break;
        }

        case 0xB: //GetTagInRangeEvent
        {
            sockSendRecvData(data, cmdbuf);
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = events[0];
            break;
        }

        case 0xC: //GetTagOutOfRangeEvent
        {
            if(data->hidthread.handle == -1)
                MyThread_Create(&data->hidthread, &hidThread, (void*)data, threadStack, 0x1000, 15, -2);
            sockSendRecvData(data, cmdbuf);
            cmdbuf[0] = IPC_MakeHeader(cmdid, 1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = events[1];
            break;
        }

        case 0xD:
        {
            if(data->connected)
            {
                if(tag_state == NFC_TagState_OutOfRange)
                {
                    cmdbuf[1] = 1;
                    cmdbuf[2] = NFC_TagState_OutOfRange;
                }
                sockSendRecvData(data, cmdbuf);
                break;
            }

            cmdbuf[0] = IPC_MakeHeader(cmdid, 2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = tag_state;
            break;
        }

        case 0xF:
        {
            cmdbuf[0] = IPC_MakeHeader(cmdid, 2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 2;
            break;
        }

        case 0x14: // InitizeAppData
        {
            sockSendRecvData(data, cmdbuf);
            u32 buf_ptr = cmdbuf[0x12];
            FS_ProgramInfo info;
            FSUSER_GetProgramLaunchInfo(&info, cmdbuf[16]);
            memcpy((u8*)&cmdbuf[0], (u8*)&info.programId, 8);                                                                                                                                                          ;
            memcpy((u8*)&cmdbuf[2], buf_ptr, 0xD8);
            sockSendRecvData(data, cmdbuf);
            break;
        }

        case 0x15: // ReadAppData we need to do it here because staticbuf
        {
            sockSendRecvData(data, cmdbuf);
            memcpy(&statbuf, (u8*)&cmdbuf[3], 0xD8);
            cmdbuf[3] = &statbuf;
            break;
        } 

        case 0x16: // WriteAppData because same reason
        {
            // first we send over cmdbuf[0] to cmdbuf[10]
            sockSendRecvData(data, cmdbuf);
            u32 buf_ptr = cmdbuf[11];
            memcpy((u8*)&cmdbuf[0], buf_ptr, 0xD8); //maximum size 0xd8
            sockSendRecvData(data, cmdbuf);
            break;
        }

        case 0x404: // We need to send some additional stuff
        {
            u32 countrycode;
            memcpy(&data->buf[0], (u8*)&cmdbuf[0], 0x100); // use data->buf as a temporary backup point
            Result ret = CFGU_GetConfigInfoBlk2(4, 0xB0000u, &countrycode);
            if(ret > 0)
                CRASH;
            memcpy((u8*)&cmdbuf[0], &data->buf[0], 0x100); // restore data
            cmdbuf[43] = countrycode;
            sockSendRecvData(data, cmdbuf);
            break;
        }

        default:
        {
            if(connfd >= 0)
            {
                sockSendRecvData(data, cmdbuf);	
            }
        }
        
    }

}

int main() {

    int nmbActiveHandles;

    Handle *hndNfuU;
    Handle *hndNfuM;
    Handle *hndNotification;
    Handle hndList[MAX_SESSIONS+SERVICE_ENDPOINTS];

    hndNotification = &hndList[0];
    hndNfuU = &hndList[1];
    hndNfuM = &hndList[2];
    nmbActiveHandles = SERVICE_ENDPOINTS;
    sockThreadStruct thread_data;

    u32* staticbuf = getThreadStaticBuffers();
    staticbuf[0]  = IPC_Desc_StaticBuffer(0x800, 0);
    staticbuf[1]  = &statbuf;
    staticbuf[2]  = IPC_Desc_StaticBuffer(0x800, 0);
    staticbuf[3]  = &statbuf;
    staticbuf[4]  = IPC_Desc_StaticBuffer(0x800, 0);
    staticbuf[5]  = &statbuf;

    u8 buf[256];
    thread_data.buf = buf;
    thread_data.hidthread.handle = -1;

    Result ret = 0;
    if (R_FAILED(srvRegisterService(hndNfuU, "nfc:u", MAX_SESSIONS))) {
        svcBreak(USERBREAK_ASSERT);
    }
    if (R_FAILED(srvRegisterService(hndNfuM, "nfc:m", MAX_SESSIONS))) {
        svcBreak(USERBREAK_ASSERT);
    }
    if (R_FAILED(srvEnableNotification(hndNotification))) {
        svcBreak(USERBREAK_ASSERT);
    }

    svcCreateEvent(&events[0], RESET_ONESHOT);
    svcCreateEvent(&events[1], RESET_ONESHOT);
    
    struct sockaddr_in servaddr;
    sockfd = socSocket(AF_INET, SOCK_STREAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = socGethostid(); 
    servaddr.sin_port = htons(8001); 
  
    // Binding newly created socket to given IP and verification 
    if ((socBind(sockfd, &servaddr, sizeof(servaddr))) != 0)
        CRASH;
    
    if ((socListen(sockfd, 1)) != 0)
        CRASH;
    
    Handle reply_target = 0;
    int term_request = 0;
    do {
        if (reply_target == 0) {
            u32 *cmdbuf = getThreadCommandBuffer();
            cmdbuf[0] = 0xFFFF0000;
        }
        s32 request_index;
        //logPrintf("B SRAR %d %x\n", request_index, reply_target);
        ret = svcReplyAndReceive(&request_index, hndList, nmbActiveHandles, reply_target);
        //logPrintf("A SRAR %d %x\n", request_index, reply_target);

        if (R_FAILED(ret)) {
            // check if any handle has been closed
            if (ret == 0xC920181A) {
                if (request_index == -1) {
                    for (int i = SERVICE_ENDPOINTS; i < MAX_SESSIONS+SERVICE_ENDPOINTS; i++) {
                        if (hndList[i] == reply_target) {
                            request_index = i;
                            break;
                        }
                    }
                }
                svcCloseHandle(hndList[request_index]);
                hndList[request_index] = hndList[nmbActiveHandles-1];
                nmbActiveHandles--;
                reply_target = 0;
            } else {
                svcBreak(USERBREAK_ASSERT);
            }
        } else {
            // process responses
            reply_target = 0;
            switch (request_index) {
                case 0: { // notification
                    if (R_FAILED(should_terminate(&term_request))) {
                        svcBreak(USERBREAK_ASSERT);
                    }
                    break;
                }
                case 1: // new session
                case 2: {// new session
                    //logPrintf("New Session %d\n", request_index);
                    Handle handle;
                    if (R_FAILED(svcAcceptSession(&handle, hndList[request_index]))) {
                        svcBreak(USERBREAK_ASSERT);
                    }
                    //logPrintf("New Session accepted %x on index %d\n", handle, nmbActiveHandles);
                    if (nmbActiveHandles < MAX_SESSIONS+SERVICE_ENDPOINTS) {
                        hndList[nmbActiveHandles] = handle;
                        nmbActiveHandles++;
                    } else {
                        svcCloseHandle(handle);
                    }
                    break;
                }
                default: { // session
                    //logPrintf("cmd handle %x\n", request_index);
                //	__asm("bkpt #0");
                    handle_commands(&thread_data);
                    reply_target = hndList[request_index];
                    break;
                }
            }
        }
    } while (!term_request);

    srvUnregisterService("nfc:m");
    srvUnregisterService("nfc:u");

    svcCloseHandle(*hndNfuM);
    svcCloseHandle(*hndNfuU);
    svcCloseHandle(*hndNotification);

    return 0;
}