#pragma once

#include "chain.h"
#include "validationstate.h"
#include <string>

std::string GetWarnings(std::string strFor);
void CheckForkWarningConditions();
void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip);
void InvalidChainFound(CBlockIndex* pindexNew);
