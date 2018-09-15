#include "warnings.h"
#include "tessa_externs.h"
#include "ui_interface.h"
#include "utiltime.h"

#include "main.h"  // for IsInitialBlockDownload
#include <cmath>

#if defined(HAVE_CONFIG_H)
#include "tessa-config.h"
#endif

//////////////////////////////////////////////////////////////////////////////
//
//
using std::string;

string GetWarnings(string strFor) {
  string strStatusBar;
  string strRPC;

  if (!CLIENT_VERSION_IS_RELEASE)
    strStatusBar =
        _("This is a pre-release test build - use at your own risk - do not use for staking or merchant applications!");

  if (GetBoolArg("-testsafemode", false)) strStatusBar = strRPC = "testsafemode enabled";

  // Misc warnings like out of disk space and clock is wrong
  if (strMiscWarning != "") { strStatusBar = strMiscWarning; }

  if (fLargeWorkForkFound) {
    strStatusBar = strRPC =
        _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
  } else if (fLargeWorkInvalidChainFound) {
    strStatusBar = strRPC =
        _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need "
          "to upgrade.");
  }

  if (strFor == "statusbar")
    return strStatusBar;
  else if (strFor == "rpc")
    return strRPC;
  assert(!"GetWarnings() : invalid parameter");
  return "error";
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = nullptr, *pindexBestForkBase = nullptr;

void CheckForkWarningConditions() {
  AssertLockHeld(cs_main);
  // Before we get past initial download, we cannot reliably alert about forks
  // (we assume we don't get stuck on a fork before the last checkpoint)
  if (IsInitialBlockDownload()) return;

  // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
  // of our head, drop it
  if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72) pindexBestForkTip = nullptr;

  if (pindexBestForkTip ||
      (pindexBestInvalid &&
       pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6))) {
    if (pindexBestForkTip && pindexBestForkBase) {
      if (pindexBestForkBase->phashBlock) {
        LogPrintf(
            "CheckForkWarningConditions: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  "
            "lasting to height %d (%s).\nChain state database corruption likely.\n",
            pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(), pindexBestForkTip->nHeight,
            pindexBestForkTip->phashBlock->ToString());
        fLargeWorkForkFound = true;
      }
    } else {
      LogPrintf(
          "CheckForkWarningConditions: Warning: Found invalid chain at least ~6 blocks longer than our best "
          "chain.\nChain state database corruption likely.\n");
      fLargeWorkInvalidChainFound = true;
    }
  } else {
    fLargeWorkForkFound = false;
    fLargeWorkInvalidChainFound = false;
  }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip) {
  AssertLockHeld(cs_main);
  // If we are on a fork that is sufficiently large, set a warning flag
  CBlockIndex* pfork = pindexNewForkTip;
  CBlockIndex* plonger = chainActive.Tip();
  while (pfork && pfork != plonger) {
    while (plonger && plonger->nHeight > pfork->nHeight) plonger = plonger->pprev;
    if (pfork == plonger) break;
    pfork = pfork->pprev;
  }

  // We define a condition which we should warn the user about as a fork of at least 7 blocks
  // who's tip is within 72 blocks (+/- 3 hours if no one mines it) of ours
  // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
  // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
  // hash rate operating on the fork.
  // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
  // the 7-block condition and from this always have the most-likely-to-cause-warning fork
  if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
      pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
      chainActive.Height() - pindexNewForkTip->nHeight < 72) {
    pindexBestForkTip = pindexNewForkTip;
    pindexBestForkBase = pfork;
  }

  CheckForkWarningConditions();
}

void InvalidChainFound(CBlockIndex* pindexNew) {
  if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork) pindexBestInvalid = pindexNew;

  LogPrintf("InvalidChainFound: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n",
            pindexNew->GetBlockHash().ToString(), pindexNew->nHeight, log(pindexNew->nChainWork.getdouble()) / log(2.0),
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
  LogPrintf("InvalidChainFound:  current best=%s  height=%d  log2_work=%.8g  date=%s\n",
            chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
            log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0),
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()));
  CheckForkWarningConditions();
}
