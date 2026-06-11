#include "server.h"

#include <cstring>

#include "hashtable.h"

void OutNil(std::string &out) { out.push_back(kSerNil); }

void OutStr(std::string &out, const std::string &val) {
  out.push_back(kSerStr);
  uint32_t len = (uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

void OutInt(std::string &out, int64_t val) {
  out.push_back(kSerInt);
  out.append((char *)&val, 8);
}

void OutErr(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(kSerErr);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

void OutArr(std::string &out, uint32_t n) {
  out.push_back(kSerArr);
  out.append((char *)&n, 4);
}

void DoGet(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = StrHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HmLookup(&g_data.db, &key.node, &EntryEq);
  if (!node) {
    return OutNil(out);
  }

  const std::string &val = container_of(node, Entry, node)->val;
  OutStr(out, val);
}

void DoSet(std::vector<std::string> &cmd, std::string &out) {
  (void)out;

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = StrHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HmLookup(&g_data.db, &key.node, &EntryEq);
  if (node) {
    container_of(node, Entry, node)->val.swap(cmd[2]);
  } else {
    Entry *ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->val.swap(cmd[2]);
    HmInsert(&g_data.db, &ent->node);
  }
  return OutNil(out);
}

void DoDel(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = StrHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HmPop(&g_data.db, &key.node, &EntryEq);
  if (node) {
    delete container_of(node, Entry, node);
  }
  OutInt(out, node ? 1 : 0);
}

void DoKeys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  OutArr(out, (uint32_t)HmSize(&g_data.db));
  HScan(&g_data.db.ht1, &CbScan, &out);
  HScan(&g_data.db.ht2, &CbScan, &out);
}

void DoExpire(std::vector<std::string> &cmd, std::string &out) {
  int64_t ttl_ms = 0;
  if (!Str2Int(cmd[2], ttl_ms)) {
    return OutErr(out, kErrArg, "expect int64");
  }

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = StrHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HmLookup(&g_data.db, &key.node, &EntryEq);
  if (node) {
    Entry *ent = container_of(node, Entry, node);
    EntrySetTtl(ent, ttl_ms);
  }
  return OutInt(out, node ? 1 : 0);
}

void DoTtl(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = StrHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HmLookup(&g_data.db, &key.node, &EntryEq);
  if (!node) {
    return OutInt(out, -2);
  }

  Entry *ent = container_of(node, Entry, node);
  if (ent->heap_idx == (size_t)-1) {
    return OutInt(out, -1);
  }

  uint64_t expire_at = g_data.heap[ent->heap_idx].val;
  uint64_t now_us = GetMonotonicUsec();
  return OutInt(out,
                expire_at > now_us ? (expire_at - now_us) / 1000 : 0);
}

void DoRequest(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && CmdIs(cmd[0], "keys")) {
    DoKeys(cmd, out);
  } else if (cmd.size() == 2 && CmdIs(cmd[0], "get")) {
    DoGet(cmd, out);
  } else if (cmd.size() == 3 && CmdIs(cmd[0], "set")) {
    DoSet(cmd, out);
  } else if (cmd.size() == 2 && CmdIs(cmd[0], "del")) {
    DoDel(cmd, out);
  } else if (cmd.size() == 3 && CmdIs(cmd[0], "expire")) {
    DoExpire(cmd, out);
  } else if (cmd.size() == 2 && CmdIs(cmd[0], "ttl")) {
    DoTtl(cmd, out);
  } else {
    OutErr(out, kErrUnknown, "Unknown cmd");
  }
}

int32_t ParseReq(const uint8_t *data, size_t len,
                 std::vector<std::string> &out) {
  if (len < 4) {
    return -1;
  }
  uint32_t n = 0;
  memcpy(&n, &data[0], 4);
  if (n > kMaxArgs) {
    return -1;
  }

  size_t pos = 4;
  while (n--) {
    if (pos + 4 > len) {
      return -1;
    }
    uint32_t sz = 0;
    memcpy(&sz, &data[pos], 4);
    if (pos + 4 + sz > len) {
      return -1;
    }
    out.push_back(std::string((char *)&data[pos + 4], sz));
    pos += 4 + sz;
  }

  if (pos != len) {
    return -1;
  }
  return 0;
}
