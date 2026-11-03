#pragma once
// Observer handshake only. No heap ownership or allocator changes.
static void pause09(const char* label) {
    printf("{\"checkpoint09\":\"%s\",\"pid\":%lu}\n",label,GetCurrentProcessId());
    fflush(stdout);
    char token=0;DWORD count=0;
    if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),&token,1,&count,nullptr)||count!=1||token!='C') {
        fprintf(stderr,"checkpoint09 acknowledgement failed\n");ExitProcess(3);
    }
}
