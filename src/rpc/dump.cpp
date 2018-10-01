// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "util.h"
#include "utilsplitstring.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "wallet/wallet.h"
#include "wallet/wallettx.h"

#include <cstdint>
#include <fstream>
#include <secp256k1.h>

#include <chrono>
#include <univalue/univalue.h>

using namespace std;
using namespace ecdsa;

void EnsureWalletIsUnlocked(bool fAllowAnonOnly);

std::string static EncodeDumpTime(int64_t nTime) { return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime); }

int64_t static DecodeDumpTime(const std::string& str) {
  ///#warning "Need to check as converted from boost, also no locale";
  std::istringstream iss(str);
  std::tm tm = {};
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
  // return (ptime - epoch).total_seconds();
}

std::string static EncodeDumpString(const std::string& str) {
  std::stringstream ret;
  for (uint8_t c : str) {
    if (c <= 32 || c >= 128 || c == '%') {
      ret << '%' << HexStr(&c, &c + 1);
    } else {
      ret << c;
    }
  }
  return ret.str();
}

std::string DecodeDumpString(const std::string& str) {
  std::stringstream ret;
  for (uint32_t pos = 0; pos < str.length(); pos++) {
    uint8_t c = str[pos];
    if (c == '%' && pos + 2 < str.length()) {
      c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
          ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
      pos += 2;
    }
    ret << c;
  }
  return ret.str();
}

UniValue importprivkey(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() < 1 || params.size() > 3)
    throw runtime_error(
        "importprivkey \"tessaprivkey\" ( \"label\" rescan )\n"
        "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n" +
        HelpRequiringPassphrase() +
        "\n"

        "\nArguments:\n"
        "1. \"tessaprivkey\"   (string, required) The private key (see dumpprivkey)\n"
        "2. \"label\"            (string, optional, default=\"\") An optional label\n"
        "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"

        "\nNote: This call can take minutes to complete if rescan is true.\n"

        "\nExamples:\n"
        "\nDump a private key\n" +
        HelpExampleCli("dumpprivkey", "\"myaddress\"") + "\nImport the private key with rescan\n" +
        HelpExampleCli("importprivkey", "\"mykey\"") + "\nImport using a label and without rescan\n" +
        HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") + "\nAs a JSON-RPC call\n" +
        HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false"));

  LOCK2(cs_main, pwalletMain->cs_wallet);

  EnsureWalletIsUnlocked();

  string strSecret = params[0].get_str();
  string strLabel = "";
  if (params.size() > 1) strLabel = params[1].get_str();

  // Whether to perform rescan after import
  bool fRescan = true;
  if (params.size() > 2) fRescan = params[2].get_bool();

  CKey key = DecodeSecret(strSecret);
  if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

  CPubKey pubkey = key.GetPubKey();
  assert(key.VerifyPubKey(pubkey));
  CKeyID vchAddress = pubkey.GetID();
  {
    pwalletMain->MarkDirty();
    pwalletMain->SetAddressBook(vchAddress, strLabel, "receive");

    // Don't throw error in case a key is already there
    if (pwalletMain->HaveKey(vchAddress)) return NullUniValue;

    pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

    if (!pwalletMain->AddKeyPubKey(key, pubkey)) throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

    // whenever a key is imported, we need to scan the whole chain
    pwalletMain->nTimeFirstKey = 1;  // 0 would be considered 'no value'

    if (fRescan) { pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true); }
  }

  return NullUniValue;
}

UniValue importaddress(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() < 1 || params.size() > 3)
    throw runtime_error(
        "importaddress \"address\" ( \"label\" rescan )\n"
        "\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to "
        "spend.\n"

        "\nArguments:\n"
        "1. \"address\"          (string, required) The address\n"
        "2. \"label\"            (string, optional, default=\"\") An optional label\n"
        "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"

        "\nNote: This call can take minutes to complete if rescan is true.\n"

        "\nExamples:\n"
        "\nImport an address with rescan\n" +
        HelpExampleCli("importaddress", "\"myaddress\"") + "\nImport using a label without rescan\n" +
        HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") + "\nAs a JSON-RPC call\n" +
        HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false"));

  LOCK2(cs_main, pwalletMain->cs_wallet);

  CScript script;
  CTxDestination address;

  if (IsValidDestinationString(params[0].get_str())) {
    address = DecodeDestination(params[0].get_str());
    script = GetScriptForDestination(address);
  } else if (IsHex(params[0].get_str())) {
    std::vector<uint8_t> data(ParseHex(params[0].get_str()));
    script = CScript(data.begin(), data.end());
  } else {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address or script");
  }

  string strLabel = "";
  if (params.size() > 1) strLabel = params[1].get_str();

  // Whether to perform rescan after import
  bool fRescan = true;
  if (params.size() > 2) fRescan = params[2].get_bool();

  {
    if (::IsMine(*pwalletMain, script) == ISMINE_SPENDABLE)
      throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

    // add to address book or update label
    pwalletMain->SetAddressBook(address, strLabel, "receive");

    // Don't throw error in case an address is already there
    if (pwalletMain->HaveWatchOnly(script)) return NullUniValue;

    pwalletMain->MarkDirty();

    if (!pwalletMain->AddWatchOnly(script)) throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

    if (fRescan) {
      pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
      pwalletMain->ReacceptWalletTransactions();
    }
  }

  return NullUniValue;
}

UniValue importwallet(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() != 1)
    throw runtime_error(
        "importwallet \"filename\"\n"
        "\nImports keys from a wallet dump file (see dumpwallet).\n" +
        HelpRequiringPassphrase() +
        "\n"

        "\nArguments:\n"
        "1. \"filename\"    (string, required) The wallet file\n"

        "\nExamples:\n"
        "\nDump the wallet\n" +
        HelpExampleCli("dumpwallet", "\"test\"") + "\nImport the wallet\n" +
        HelpExampleCli("importwallet", "\"test\"") + "\nImport using the json rpc call\n" +
        HelpExampleRpc("importwallet", "\"test\""));

  LOCK2(cs_main, pwalletMain->cs_wallet);

  EnsureWalletIsUnlocked();

  ifstream file;
  file.open(params[0].get_str().c_str(), std::ios::in | std::ios::ate);
  if (!file.is_open()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

  int64_t nTimeBegin = chainActive.Tip()->GetBlockTime();

  bool fGood = true;

  int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
  file.seekg(0, file.beg);

  pwalletMain->ShowProgress.fire(_("Importing..."), 0);  // show progress dialog in GUI
  while (file.good()) {
    pwalletMain->ShowProgress.fire("",
                                   std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
    std::string line;
    std::getline(file, line);
    if (line.empty() || line[0] == '#') continue;

    std::vector<std::string> vstr;
    Split(vstr, line, " ");
    if (vstr.size() < 2) continue;
    CKey key = DecodeSecret(vstr[0]);
    if (!key.IsValid()) continue;
    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID keyid = pubkey.GetID();
    if (pwalletMain->HaveKey(keyid)) {
      LogPrintf("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
      continue;
    }
    int64_t nTime = DecodeDumpTime(vstr[1]);
    std::string strLabel;
    bool fLabel = true;
    for (uint32_t nStr = 2; nStr < vstr.size(); nStr++) {
      if (vstr[nStr].substr(0, 1) == "#") break;
      if (vstr[nStr] == "change=1") fLabel = false;
      if (vstr[nStr] == "reserve=1") fLabel = false;
      if (vstr[nStr].substr(0, 6) == "label=") {
        strLabel = DecodeDumpString(vstr[nStr].substr(6));
        fLabel = true;
      }
    }
    LogPrintf("Importing %s...\n", EncodeDestination(keyid));
    if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
      fGood = false;
      continue;
    }
    pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
    if (fLabel) pwalletMain->SetAddressBook(keyid, strLabel, "receive");
    nTimeBegin = std::min(nTimeBegin, nTime);
  }
  file.close();
  pwalletMain->ShowProgress.fire("", 100);  // hide progress dialog in GUI

  CBlockIndex* pindex = chainActive.Tip();
  while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200) pindex = pindex->pprev;

  if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey) pwalletMain->nTimeFirstKey = nTimeBegin;

  LogPrintf("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
  pwalletMain->ScanForWalletTransactions(pindex);
  pwalletMain->MarkDirty();

  if (!fGood) throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

  return NullUniValue;
}

UniValue dumpprivkey(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() != 1)
    throw runtime_error(
        "dumpprivkey \"tessaaddress\"\n"
        "\nReveals the private key corresponding to 'tessaaddress'.\n"
        "Then the importprivkey can be used with this output\n" +
        HelpRequiringPassphrase() +
        "\n"

        "\nArguments:\n"
        "1. \"tessaaddress\"   (string, required) The tessa address for the private key\n"

        "\nResult:\n"
        "\"key\"                (string) The private key\n"

        "\nExamples:\n" +
        HelpExampleCli("dumpprivkey", "\"myaddress\"") + HelpExampleCli("importprivkey", "\"mykey\"") +
        HelpExampleRpc("dumpprivkey", "\"myaddress\""));

  LOCK2(cs_main, pwalletMain->cs_wallet);

  EnsureWalletIsUnlocked();

  string strAddress = params[0].get_str();
  if (!IsValidDestinationString(strAddress)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
  CTxDestination address = DecodeDestination(strAddress);
  CKeyID* keyID = &std::get<CKeyID>(address);
  if (!keyID) throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
  CKey vchSecret;
  if (!pwalletMain->GetKey(*keyID, vchSecret))
    throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
  return EncodeSecret(vchSecret);
}

UniValue dumpwallet(const UniValue& params, bool fHelp) {
  if (fHelp || params.size() != 1)
    throw runtime_error(
        "dumpwallet \"filename\"\n"
        "\nDumps all wallet keys in a human-readable format.\n" +
        HelpRequiringPassphrase() +
        "\n"

        "\nArguments:\n"
        "1. \"filename\"    (string, required) The filename\n"

        "\nExamples:\n" +
        HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

  LOCK2(cs_main, pwalletMain->cs_wallet);

  EnsureWalletIsUnlocked();

  ofstream file;
  file.open(params[0].get_str().c_str());
  if (!file.is_open()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

  std::map<CKeyID, int64_t> mapKeyBirth;
  std::set<CKeyID> setKeyPool;
  pwalletMain->GetKeyBirthTimes(mapKeyBirth);
  pwalletMain->GetAllReserveKeys(setKeyPool);

  // sort time/key pairs
  std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
  // pair is reversed
  for (auto& it : mapKeyBirth) vKeyBirth.push_back(std::make_pair(it.second, it.first));
  mapKeyBirth.clear();
  std::sort(vKeyBirth.begin(), vKeyBirth.end());

  // produce output
  file << strprintf("# Wallet dump created by Tessa %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
  file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
  file << strprintf("# * Best block at time of backup was %i (%s),\n", chainActive.Height(),
                    chainActive.Tip()->GetBlockHash().ToString());
  file << strprintf("#   mined on %s\n", EncodeDumpTime(chainActive.Tip()->GetBlockTime()));
  file << "\n";

  // add the base58check encoded extended master if the wallet uses HD
  CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
  if (!masterKeyID.IsNull()) {
    CKey key;
    if (pwalletMain->GetKey(masterKeyID, key)) {
      CExtKey masterKey;
      masterKey.SetMaster(key.begin(), key.size());

#ifdef SDFDF
#warning "Fix this...."
      CBitcoinExtKey b58extkey;
      b58extkey.SetKey(masterKey);
      file << "# extended private masterkey: " << b58extkey.ToString() << "\n\n";
#endif
    }
  }

  for (const auto& it : vKeyBirth) {
    const CKeyID& keyid = it.second;
    std::string strTime = EncodeDumpTime(it.first);
    std::string strAddr = EncodeDestination(keyid);
    CKey key;
    if (pwalletMain->GetKey(keyid, key)) {
      file << strprintf("%s %s ", EncodeSecret(key), strTime);
      if (pwalletMain->mapAddressBook.count(keyid)) {
        file << strprintf("%s %s label=%s # addr=%s\n", EncodeSecret(key), strTime,
                          EncodeDumpString(pwalletMain->mapAddressBook[keyid].name), strAddr);
      } else if (keyid == masterKeyID) {
        file << "hdmaster=1";
      } else if (setKeyPool.count(keyid)) {
        file << "reserve=1";
      } else if (pwalletMain->mapKeyMetadata[keyid].hdKeypath == "m") {
        file << "inactivehdmaster=1";
      } else {
        file << "change=1";
      }
      file << strprintf(" # addr=%s%s\n", strAddr,
                        (pwalletMain->mapKeyMetadata[keyid].hdKeypath.size() > 0
                             ? " hdkeypath=" + pwalletMain->mapKeyMetadata[keyid].hdKeypath
                             : ""));
    }
  }
  file << "\n";
  file << "# End of dump\n";
  file.close();
  return NullUniValue;
}
