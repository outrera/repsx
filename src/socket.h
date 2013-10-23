#ifndef __SOCKET_H__
#define __SOCKET_H__

int StartServer();
void StopServer();

void GetClient();
void CloseClient();

int HasClient();

int ReadSocket(char * buffer, int len);
int RawReadSocket(char * buffer, int len);
void WriteSocket(char * buffer, int len);

void SetsBlock();
void SetsNonblock();

#endif
