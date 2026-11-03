#pragma once
// Same four-byte environment selector; original replay ignores this variable.
static void observer11(const char* label) {
    char mode[5]{};
    if(GetEnvironmentVariableA("SE_OBSERVER11",mode,sizeof(mode))!=4) {
        fprintf(stderr,"invalid observer11 selector\n");ExitProcess(3);
    }
    if(strcmp(mode,"off_")==0)return;
    const bool ack=strcmp(mode,"ack_")==0;
    if(!ack&&strcmp(mode,"mark")!=0){fprintf(stderr,"unknown observer11 selector\n");ExitProcess(3);}
    printf("{\"observer11\":\"%s\",\"pid\":%lu}\n",label,GetCurrentProcessId());
    fflush(stdout);
    if(ack){char token=0;DWORD count=0;if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),&token,1,&count,nullptr)||count!=1||token!='C'){fprintf(stderr,"observer11 acknowledgement failed\n");ExitProcess(3);}}
}
