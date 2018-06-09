#pragma once

#include <string>
#include "validationstate.h"
#include "chain.h"

std::string GetWarnings(std::string strFor);
void CheckForkWarningConditions();
void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip);
void InvalidChainFound(CBlockIndex* pindexNew);
