#ifndef REDIS_SERVER_H_
#define REDIS_SERVER_H_

#include <assert.h>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "hashtable.h"
#include "heap.h"
#include "list.h"

const size_t kMaxMsg = 4096;
const size_t kMaxArgs = 1024;
const uint64_t kIdleTimeoutMs = 5 * 1000;
const int kMaxEvents = 64;

enum State {
  kStateReq = 0,
  kStateRes = 1,
  kStateEnd = 2,
};

enum SerType {
  kSerNil = 0,
  kSerErr = 1,
  kSerStr = 2,
  kSerInt = 3,
  kSerArr = 4,
};

enum ErrCode {
  kErrUnknown = 1,
  kErr2Big = 2,
  kErrArg = 4,
  kErrType = 5,
};

enum EntryType {
  kTypeStr = 0,
  kTypeZset = 1,
};

struct ZSet;

struct Conn {
  int fd = -1;
  uint32_t state = kStateReq;
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + kMaxMsg];
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + kMaxMsg];
  uint64_t idle_start = 0;
  DList idle_list;
};

struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
  uint32_t type = 0;
  ZSet *zset = nullptr;
  size_t heap_idx = -1;
};

struct GlobalData {
  HMap db;
  std::vector<Conn *> fd2conn;
  DList idle_list;
  int epfd = -1;
  std::vector<HeapItem> heap;
};

extern GlobalData g_data;

// connection
void FdSetNb(int fd);
void ConnPut(std::vector<Conn *> &fd2conn, struct Conn *conn);
int32_t AcceptNewConn(int fd, int epfd);
void ConnectionIo(Conn *conn);
bool TryFillBuffer(Conn *conn);
void StateReq(Conn *conn);
bool TryOneRequest(Conn *conn);
void StateRes(Conn *conn);
bool TryFlushBuffer(Conn *conn);
void ConnDone(Conn *conn);

// timer
uint64_t GetMonotonicUsec();
uint32_t NextTimerMs();
void ProcessTimers();

// serialization
void OutNil(std::string &out);
void OutStr(std::string &out, const std::string &val);
void OutInt(std::string &out, int64_t val);
void OutErr(std::string &out, int32_t code, const std::string &msg);
void OutArr(std::string &out, uint32_t n);

// command
void DoRequest(std::vector<std::string> &cmd, std::string &out);
int32_t ParseReq(const uint8_t *data, size_t len,
                 std::vector<std::string> &out);
bool EntryEq(HNode *lhs, HNode *rhs);
bool HnodeSame(HNode *lhs, HNode *rhs);
void CbScan(HNode *node, void *arg);
void DoGet(std::vector<std::string> &cmd, std::string &out);
void DoSet(std::vector<std::string> &cmd, std::string &out);
void DoDel(std::vector<std::string> &cmd, std::string &out);
void DoKeys(std::vector<std::string> &cmd, std::string &out);
void DoExpire(std::vector<std::string> &cmd, std::string &out);
void DoTtl(std::vector<std::string> &cmd, std::string &out);

// util
void Msg(const char *s);
bool CmdIs(const std::string &word, const char *cmd);
bool Str2Int(const std::string &s, int64_t &out);
void EntrySetTtl(Entry *ent, int64_t ttl_ms);
void EntryDel(Entry *ent);
void ZsetDispose(ZSet *zset);

#endif  // REDIS_SERVER_H_
