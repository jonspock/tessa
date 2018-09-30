// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"

#include "init.h"
#include "main.h"
#include "random.h"
#include "sync.h"
#include "tessa_externs.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"

#include "signals-cpp/signals.hpp"
#include <univalue/univalue.h>

using namespace RPCServer;
using namespace std;
using std::placeholders::_1;

static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static std::string rpcWarmupStatus("RPC server started");
static CCriticalSection cs_rpcWarmup;

/* Timer-creating functions */
static std::vector<RPCTimerInterface*> timerInterfaces;
/* Map of name to timer. */
static std::map<std::string, std::unique_ptr<RPCTimerBase> > deadlineTimers;

static struct CRPCSignals {
  sigs::signal<void()> Started;
  sigs::signal<void()> Stopped;
  sigs::signal<void(const CRPCCommand&)> PreCommand;
  sigs::signal<void(const CRPCCommand&)> PostCommand;
} g_rpcSignals;

void RPCServer::OnStarted(std::function<void()> slot) { g_rpcSignals.Started.connect(slot); }

void RPCServer::OnStopped(std::function<void()> slot) { g_rpcSignals.Stopped.connect(slot); }

void RPCServer::OnPreCommand(std::function<void(const CRPCCommand&)> slot) {
  g_rpcSignals.PreCommand.connect(std::bind(slot, _1));
}

void RPCServer::OnPostCommand(std::function<void(const CRPCCommand&)> slot) {
  g_rpcSignals.PostCommand.connect(std::bind(slot, _1));
}

void RPCTypeCheck(const UniValue& params, const list<UniValue::VType>& typesExpected, bool fAllowNull) {
  uint32_t i = 0;
  for (UniValue::VType t : typesExpected) {
    if (params.size() <= i) break;

    const UniValue& v = params[i];
    if (!((v.type() == t) || (fAllowNull && (v.isNull())))) {
      string err = strprintf("Expected type %s, got %s", uvTypeName(t), uvTypeName(v.type()));
      throw JSONRPCError(RPC_TYPE_ERROR, err);
    }
    i++;
  }
}

void RPCTypeCheckObj(const UniValue& o, const map<string, UniValue::VType>& typesExpected, bool fAllowNull) {
  for (const auto& t : typesExpected) {
    const UniValue& v = find_value(o, t.first);
    if (!fAllowNull && v.isNull()) throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));

    if (!((v.type() == t.second) || (fAllowNull && (v.isNull())))) {
      string err = strprintf("Expected type %s for %s, got %s", uvTypeName(t.second), t.first, uvTypeName(v.type()));
      throw JSONRPCError(RPC_TYPE_ERROR, err);
    }
  }
}

static inline int64_t roundint64(double d) { return (int64_t)(d > 0 ? d + 0.5 : d - 0.5); }

CAmount AmountFromValue(const UniValue& value) {
  double dAmount = value.get_real();
  if (dAmount <= 0.0 || dAmount > 21000000.0) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
  CAmount nAmount = roundint64(dAmount * COIN);
  if (!MoneyRange(nAmount)) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
  return nAmount;
}

UniValue ValueFromAmount(const CAmount& amount) {
  bool sign = amount < 0;
  int64_t n_abs = (sign ? -amount : amount);
  int64_t quotient = n_abs / COIN;
  int64_t remainder = n_abs % COIN;
  return UniValue(UniValue::VNUM, strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder));
}

uint256 ParseHashV(const UniValue& v, string strName) {
  string strHex;
  if (v.isStr()) strHex = v.get_str();
  if (!IsHex(strHex))  // Note: IsHex("") is false
    throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
  uint256 result;
  result.SetHex(strHex);
  return result;
}
uint256 ParseHashO(const UniValue& o, string strKey) { return ParseHashV(find_value(o, strKey), strKey); }
vector<uint8_t> ParseHexV(const UniValue& v, string strName) {
  string strHex;
  if (v.isStr()) strHex = v.get_str();
  if (!IsHex(strHex))
    throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
  return ParseHex(strHex);
}
vector<uint8_t> ParseHexO(const UniValue& o, string strKey) { return ParseHexV(find_value(o, strKey), strKey); }

int ParseInt(const UniValue& o, string strKey) {
  const UniValue& v = find_value(o, strKey);
  if (v.isNum()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, " + strKey + "is not an int");

  return v.get_int();
}

bool ParseBool(const UniValue& o, string strKey) {
  const UniValue& v = find_value(o, strKey);
  if (v.isBool()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, " + strKey + "is not a bool");

  return v.get_bool();
}

/**
 * Note: This interface may still be subject to change.
 */

string CRPCTable::help(string strCommand) const {
  string strRet;
  string category;
  set<rpcfn_type> setDone;
  vector<pair<string, const CRPCCommand*> > vCommands;

  for (auto& mi : mapCommands) vCommands.push_back(make_pair(mi.second->category + mi.first, mi.second));
  sort(vCommands.begin(), vCommands.end());

  for (const auto& command : vCommands) {
    const CRPCCommand* pcmd = command.second;
    string strMethod = pcmd->name;
    // We already filter duplicates, but these deprecated screw up the sort order
    if (strMethod.find("label") != string::npos) continue;
    if ((strCommand != "" || pcmd->category == "hidden") && strMethod != strCommand) continue;

    if (!fDisableWallet) {
      if (pcmd->reqWallet && !pwalletMain) continue;
    }

    try {
      UniValue params;
      rpcfn_type pfn = pcmd->actor;
      if (setDone.insert(pfn).second) (*pfn)(params, true);
    } catch (std::exception& e) {
      // Help text is returned in an exception
      string strHelp = string(e.what());
      if (strCommand == "") {
        if (strHelp.find('\n') != string::npos) strHelp = strHelp.substr(0, strHelp.find('\n'));

        if (category != pcmd->category) {
          if (!category.empty()) strRet += "\n";
          category = pcmd->category;
          string firstLetter(1, ToUpper(category[0]));  //.substr(0, 1);
          strRet += "== " + firstLetter + category.substr(1) + " ==\n";
        }
      }
      strRet += strHelp + "\n";
    }
  }
  if (strRet == "") strRet = strprintf("help: unknown command: %s\n", strCommand);
  strRet = strRet.substr(0, strRet.size() - 1);
  return strRet;
}

UniValue help(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() > 1)
    throw runtime_error(
        "help ( \"command\" )\n"
        "\nList all commands, or get help for a specified command.\n"
        "\nArguments:\n"
        "1. \"command\"     (string, optional) The command to get help on\n"
        "\nResult:\n"
        "\"text\"     (string) The help text\n");

  string strCommand;
  if (params.size() > 0) strCommand = params[0].get_str();

  return tableRPC.help(strCommand);
}

UniValue stop(const UniValue& params, bool fHelp) {
  // Accept the deprecated and ignored 'detach' boolean argument
  if (fHelp || params.size() > 1)
    throw runtime_error(
        "stop\n"
        "\nStop Tessa server.");
  // Event loop will exit after current HTTP requests have been handled, so
  // this reply will get back to the client.
  StartShutdown();
  return "Tessa server stopping";
}

/**
 * Call Table
 */
static const CRPCCommand vRPCCommands[] = {
    //  category              name                      actor (function)         okSafeMode threadSafe reqWallet
    //  --------------------- ------------------------  -----------------------  ---------- ---------- ---------
    /* Overall control/query calls */
    {"control", "getinfo", &getinfo, true, false, false}, /* uses wallet if enabled */
    {"control", "help", &help, true, true, false},
    {"control", "stop", &stop, true, true, false},

    /* P2P networking */
    {"network", "getnetworkinfo", &getnetworkinfo, true, false, false},
    {"network", "addnode", &addnode, true, true, false},
    {"network", "disconnectnode", &disconnectnode, true, true, false},
    {"network", "getaddednodeinfo", &getaddednodeinfo, true, true, false},
    {"network", "getconnectioncount", &getconnectioncount, true, false, false},
    {"network", "getnettotals", &getnettotals, true, true, false},
    {"network", "getpeerinfo", &getpeerinfo, true, false, false},
    {"network", "ping", &ping, true, false, false},
    {"network", "setban", &setban, true, false, false},
    {"network", "listbanned", &listbanned, true, false, false},
    {"network", "clearbanned", &clearbanned, true, false, false},

    /* Block chain and UTXO */
    {"blockchain", "findserial", &findserial, true, false, false},
    {"blockchain", "getblockchaininfo", &getblockchaininfo, true, false, false},
    {"blockchain", "getbestblockhash", &getbestblockhash, true, false, false},
    {"blockchain", "getblockcount", &getblockcount, true, false, false},
    {"blockchain", "getblock", &getblock, true, false, false},
    {"blockchain", "getblockhash", &getblockhash, true, false, false},
    {"blockchain", "getblockheader", &getblockheader, false, false, false},
    {"blockchain", "getchaintips", &getchaintips, true, false, false},
    {"blockchain", "getdifficulty", &getdifficulty, true, false, false},
    {"blockchain", "getfeeinfo", &getfeeinfo, true, false, false},
    {"blockchain", "getmempoolinfo", &getmempoolinfo, true, true, false},
    {"blockchain", "getrawmempool", &getrawmempool, true, false, false},
    {"blockchain", "gettxout", &gettxout, true, false, false},
    {"blockchain", "gettxoutsetinfo", &gettxoutsetinfo, true, false, false},
    {"blockchain", "invalidateblock", &invalidateblock, true, true, false},
    {"blockchain", "reconsiderblock", &reconsiderblock, true, true, false},
    {"blockchain", "verifychain", &verifychain, true, false, false},

    /* Mining */
    {"mining", "getblocktemplate", &getblocktemplate, true, false, false},
    {"mining", "getmininginfo", &getmininginfo, true, false, false},
    {"mining", "getnetworkhashps", &getnetworkhashps, true, false, false},
    {"mining", "prioritisetransaction", &prioritisetransaction, true, false, false},
    {"mining", "submitblock", &submitblock, true, true, false},
    {"mining", "reservebalance", &reservebalance, true, true, false},

    /* Coin generation */
    {"generating", "getgenerate", &getgenerate, true, false, false},
    {"generating", "gethashespersec", &gethashespersec, true, false, false},
    {"generating", "setgenerate", &setgenerate, true, true, false},

    /* Raw transactions */
    {"rawtransactions", "createrawtransaction", &createrawtransaction, true, false, false},
    {"rawtransactions", "decoderawtransaction", &decoderawtransaction, true, false, false},
    {"rawtransactions", "decodescript", &decodescript, true, false, false},
    {"rawtransactions", "getrawtransaction", &getrawtransaction, true, false, false},
    {"rawtransactions", "sendrawtransaction", &sendrawtransaction, false, false, false},
    {"rawtransactions", "signrawtransaction", &signrawtransaction, false, false, false}, /* uses wallet if enabled */

    /* Utility functions */
    {"util", "createmultisig", &createmultisig, true, true, false},
    {"util", "validateaddress", &validateaddress, true, false, false}, /* uses wallet if enabled */
    {"util", "verifymessage", &verifymessage, true, false, false},
    {"util", "estimatefee", &estimatefee, true, true, false},
    {"util", "estimatepriority", &estimatepriority, true, true, false},

    /* Not shown in help */
    {"hidden", "invalidateblock", &invalidateblock, true, true, false},
    {"hidden", "reconsiderblock", &reconsiderblock, true, true, false},
    {"hidden", "setmocktime", &setmocktime, true, false, false},

    /* Tessa features */
    {"tessa", "spork", &spork, true, true, false},

    /* Wallet */
    {"wallet", "addmultisigaddress", &addmultisigaddress, true, false, true},
    {"wallet", "autocombinerewards", &autocombinerewards, false, false, true},
    {"wallet", "dumpprivkey", &dumpprivkey, true, false, true},
    {"wallet", "dumpwallet", &dumpwallet, true, false, true},
    {"wallet", "getaccountaddress", &getaccountaddress, true, false, true},
    {"wallet", "getaccount", &getaccount, true, false, true},
    {"wallet", "getaddressesbyaccount", &getaddressesbyaccount, true, false, true},
    {"wallet", "getbalance", &getbalance, false, false, true},
    {"wallet", "getnewaddress", &getnewaddress, true, false, true},
    {"wallet", "getrawchangeaddress", &getrawchangeaddress, true, false, true},
    {"wallet", "getreceivedbyaccount", &getreceivedbyaccount, false, false, true},
    {"wallet", "getreceivedbyaddress", &getreceivedbyaddress, false, false, true},
    {"wallet", "getstakingstatus", &getstakingstatus, false, false, true},
    {"wallet", "getstakesplitthreshold", &getstakesplitthreshold, false, false, true},
    {"wallet", "gettransaction", &gettransaction, false, false, true},
    {"wallet", "getunconfirmedbalance", &getunconfirmedbalance, false, false, true},
    {"wallet", "getwalletinfo", &getwalletinfo, false, false, true},
    {"wallet", "importprivkey", &importprivkey, true, false, true},
    {"wallet", "importwallet", &importwallet, true, false, true},
    {"wallet", "importaddress", &importaddress, true, false, true},
    {"wallet", "keypoolrefill", &keypoolrefill, true, false, true},
    {"wallet", "listaccounts", &listaccounts, false, false, true},
    {"wallet", "listaddressgroupings", &listaddressgroupings, false, false, true},
    {"wallet", "listlockunspent", &listlockunspent, false, false, true},
    {"wallet", "listreceivedbyaccount", &listreceivedbyaccount, false, false, true},
    {"wallet", "listreceivedbyaddress", &listreceivedbyaddress, false, false, true},
    {"wallet", "listsinceblock", &listsinceblock, false, false, true},
    {"wallet", "listtransactions", &listtransactions, false, false, true},
    {"wallet", "listunspent", &listunspent, false, false, true},
    {"wallet", "lockunspent", &lockunspent, true, false, true},
    {"wallet", "move", &movecmd, false, false, true},
    {"wallet", "multisend", &multisend, false, false, true},
    {"wallet", "sendfrom", &sendfrom, false, false, true},
    {"wallet", "sendmany", &sendmany, false, false, true},
    {"wallet", "sendtoaddress", &sendtoaddress, false, false, true},
    {"wallet", "sendtoaddressix", &sendtoaddressix, false, false, true},
    {"wallet", "setaccount", &setaccount, true, false, true},
    {"wallet", "setstakesplitthreshold", &setstakesplitthreshold, false, false, true},
    {"wallet", "settxfee", &settxfee, true, false, true},
    {"wallet", "signmessage", &signmessage, true, false, true},
    {"wallet", "walletlock", &walletlock, true, false, true},
    {"wallet", "walletpassphrasechange", &walletpassphrasechange, true, false, true},
    {"wallet", "walletpassphrase", &walletpassphrase, true, false, true},
    {"wallet", "getmasterHDseed", &getMasterHDseed, false, false, true},
    {"wallet", "setmasterHDseed", &setMasterHDseed, false, false, true},

    {"zerocoin", "getzerocoinbalance", &getzerocoinbalance, false, false, true},
    {"zerocoin", "listmintedzerocoins", &listmintedzerocoins, false, false, true},
    {"zerocoin", "listspentzerocoins", &listspentzerocoins, false, false, true},
    {"zerocoin", "listzerocoinamounts", &listzerocoinamounts, false, false, true},
    {"zerocoin", "mintzerocoin", &mintzerocoin, false, false, true},
    {"zerocoin", "spendzerocoin", &spendzerocoin, false, false, true},
    {"zerocoin", "resetmintzerocoin", &resetmintzerocoin, false, false, true},
    {"zerocoin", "resetspentzerocoin", &resetspentzerocoin, false, false, true},
    {"zerocoin", "getarchivedzerocoin", &getarchivedzerocoin, false, false, true},
    {"zerocoin", "exportzerocoins", &exportzerocoins, false, false, true},
    {"zerocoin", "getspentzerocoinamount", &getspentzerocoinamount, false, false, false},
    {"zerocoin", "generatemintlist", &generatemintlist, false, false, true},
    {"zerocoin", "searchdzkp", &searchdzkp, false, false, true},
    {"zerocoin", "dzkpstate", &dzkpstate, false, false, true}

};

CRPCTable::CRPCTable() {
  uint32_t vcidx;
  for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++) {
    const CRPCCommand* pcmd;

    pcmd = &vRPCCommands[vcidx];
    mapCommands[pcmd->name] = pcmd;
  }
}

const CRPCCommand* CRPCTable::operator[](const std::string& name) const {
  map<string, const CRPCCommand*>::const_iterator it = mapCommands.find(name);
  if (it == mapCommands.end()) return nullptr;
  return (*it).second;
}

bool StartRPC() {
  LogPrint(TessaLog::RPC, "Starting RPC\n");
  fRPCRunning = true;
  g_rpcSignals.Started.fire();
  return true;
}

void InterruptRPC() {
  LogPrint(TessaLog::RPC, "Interrupting RPC\n");
  // Interrupt e.g. running longpolls
  fRPCRunning = false;
}

void StopRPC() {
  LogPrint(TessaLog::RPC, "Stopping RPC\n");
  deadlineTimers.clear();
  g_rpcSignals.Stopped.fire();
}

bool IsRPCRunning() { return fRPCRunning; }

void SetRPCWarmupStatus(const std::string& newStatus) {
  LOCK(cs_rpcWarmup);
  rpcWarmupStatus = newStatus;
}

void SetRPCWarmupFinished() {
  LOCK(cs_rpcWarmup);
  assert(fRPCInWarmup);
  fRPCInWarmup = false;
}

bool RPCIsInWarmup(std::string* outStatus) {
  LOCK(cs_rpcWarmup);
  if (outStatus) *outStatus = rpcWarmupStatus;
  return fRPCInWarmup;
}

void JSONRequest::parse(const UniValue& valRequest) {
  // Parse request
  if (!valRequest.isObject()) throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
  const UniValue& request = valRequest.get_obj();

  // Parse id now so errors from here on will have the id
  id = find_value(request, "id");

  // Parse method
  UniValue valMethod = find_value(request, "method");
  if (valMethod.isNull()) throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
  if (!valMethod.isStr()) throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
  strMethod = valMethod.get_str();
  if (strMethod != "getblocktemplate")
    LogPrint(TessaLog::RPC, "ThreadRPCServer method=%s\n", SanitizeString(strMethod));

  // Parse params
  UniValue valParams = find_value(request, "params");
  if (valParams.isArray())
    params = valParams.get_array();
  else if (valParams.isNull())
    params = UniValue(UniValue::VARR);
  else
    throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
}

static UniValue JSONRPCExecOne(const UniValue& req) {
  UniValue rpc_result(UniValue::VOBJ);

  JSONRequest jreq;
  try {
    jreq.parse(req);

    UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);
    rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id);
  } catch (const UniValue& objError) {
    rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id);
  } catch (std::exception& e) {
    rpc_result = JSONRPCReplyObj(NullUniValue, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
  }

  return rpc_result;
}

std::string JSONRPCExecBatch(const UniValue& vReq) {
  UniValue ret(UniValue::VARR);
  for (uint32_t reqIdx = 0; reqIdx < vReq.size(); reqIdx++) ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

  return ret.write() + "\n";
}

UniValue CRPCTable::execute(const std::string& strMethod, const UniValue& params) const {
  // Find method
  const CRPCCommand* pcmd = tableRPC[strMethod];
  if (!pcmd) throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

  g_rpcSignals.PreCommand.fire(*pcmd);

  try {
    // Execute
    return pcmd->actor(params, false);
  } catch (std::exception& e) { throw JSONRPCError(RPC_MISC_ERROR, e.what()); }

  g_rpcSignals.PostCommand.fire(*pcmd);
}

std::vector<std::string> CRPCTable::listCommands() const {
  std::vector<std::string> commandList;
  typedef std::map<std::string, const CRPCCommand*> commandMap;

  std::transform(mapCommands.begin(), mapCommands.end(), std::back_inserter(commandList),
                 std::bind(&commandMap::value_type::first, _1));
  return commandList;
}

std::string HelpExampleCli(string methodname, string args) { return "> tessa-cli " + methodname + " " + args + "\n"; }

std::string HelpExampleRpc(string methodname, string args) {
  return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
         "\"method\": \"" +
         methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:44443/\n";
}

void RPCRegisterTimerInterface(RPCTimerInterface* iface) { timerInterfaces.push_back(iface); }

void RPCUnregisterTimerInterface(RPCTimerInterface* iface) {
  std::vector<RPCTimerInterface*>::iterator i = std::find(timerInterfaces.begin(), timerInterfaces.end(), iface);
  assert(i != timerInterfaces.end());
  timerInterfaces.erase(i);
}

void RPCRunLater(const std::string& name, std::function<void(void)> func, int64_t nSeconds) {
  if (timerInterfaces.empty()) throw JSONRPCError(RPC_INTERNAL_ERROR, "No timer handler registered for RPC");
  deadlineTimers.erase(name);
  RPCTimerInterface* timerInterface = timerInterfaces[0];
  LogPrint(TessaLog::RPC, "queue run of timer %s in %i seconds (using %s)\n", name, nSeconds, timerInterface->Name());
  deadlineTimers.insert(
      std::make_pair(name, std::unique_ptr<RPCTimerBase>(timerInterface->NewTimer(func, nSeconds * 1000))));
}

const CRPCTable tableRPC;
