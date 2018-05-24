#pragma once

class CWalletTx;

class COutput
{
public:
    const CWalletTx* tx;
    int i;
    int nDepth;
    bool fSpendable;

    COutput(const CWalletTx* txIn, int iIn, int nDepthIn, bool fSpendableIn)
    {
        tx = txIn;
        i = iIn;
        nDepth = nDepthIn;
        fSpendable = fSpendableIn;
    }

    CAmount Value() const;

    std::string ToString() const;
};


