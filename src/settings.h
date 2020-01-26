#ifndef SETTINGS_H
#define SETTINGS_H

#include "precompiled.h"
#include "camount.h"

using json = nlohmann::json;

struct Config {
    QString server;
};

struct ToFields;
struct Tx;

struct PaymentURI {
    QString addr;
    QString amt;
    QString memo;

    // Any errors are stored here
    QString error;
};


// exchanges ARW trades on that we may display in the UI
enum Exchange {
    INVALID = -1,
    SAFETRADE = 0,
    NUM
};

// everything to know about ARW for a given exchange
class ARWPriceInfo {
public:
    Exchange e;
    double btcusd;
    double arwbtc;
    double volume;
    double price_change_percent;

    double arwusd () const {
        if (e != Exchange::INVALID && btcusd != 0.0) { return arwbtc * btcusd; }
        else return 0.0;
    }
    QString to_string() {
        QString ret("");
        QTextStream(&ret) << e << ":btcusd:" << btcusd
            << ":arwbtc:" << arwbtc
            << ":arwusd:" << arwusd()
            << ":volume:" << volume
            << ":price_change_percent:" << price_change_percent;
        return ret;
    }

    ARWPriceInfo() { e = Exchange::INVALID; btcusd = 0.0; arwbtc = 0.0; volume = 0.0; price_change_percent = 0.0; }
};


class Settings
{
public:
    static  Settings* init();
    static  Settings* getInstance();

    Config  getSettings();
    void    saveSettings(const QString& server);

    bool    isTestnet();
    void    setTestnet(bool isTestnet);
            
    bool    isSaplingAddress(QString addr);
    bool    isSproutAddress(QString addr);
            
    bool    isValidSaplingPrivateKey(QString pk);

    bool    isSyncing();
    void    setSyncing(bool syncing);

    QString getZcashdVersion();
    void    setZcashdVersion(QString version);
    
    void    setUseEmbedded(bool r) { _useEmbedded = r; }
    bool    useEmbedded() { return _useEmbedded; }

    void    setHeadless(bool h) { _headless = h; }
    bool    isHeadless() { return _headless; }

    int     getBlockNumber();
    void    setBlockNumber(int number);
            
    bool    getAllowFetchPrices();
    void    setAllowFetchPrices(bool allow);

    bool    getCheckForUpdates();
    void    setCheckForUpdates(bool allow);

    QString get_theme_name();
    void set_theme_name(QString theme_name);

    bool    isSaplingActive();

    void setARWPriceInfo(ARWPriceInfo& api) { safetradePriceInfo = api; } // copy by value
    const ARWPriceInfo& getARWPriceInfo() { return safetradePriceInfo; }

    // for now - refactor as more exchanges. may want cmc valuation or some other metric
    // to be default, tbd -- i think user customizability might be a good option to add.
    double getARWPrice() { return safetradePriceInfo.arwusd(); }

    // Static stuff
    static const QString txidStatusMessage;
    
    static void saveRestore(QDialog* d);
    static void saveRestoreTableHeader(QTableView* table, QDialog* d, QString tablename) ;

    static void openAddressInExplorer(QString address);
    static void openTxInExplorer(QString txid);

    static PaymentURI parseURI(QString paymentURI);
    static QString    paymentURIPretty(PaymentURI);

    static bool    isZAddress(QString addr);
    static bool    isTAddress(QString addr);

    static QString getTokenName();
    static QString getDonationAddr();

    static QString getDefaultServer();
    static CAmount getMinerFee();

    static int     getMaxMobileAppTxns() { return 30; }

    static int     getNumberOfDecimalPlaces() {return 8;}
    
    static bool    isValidAddress(QString addr);

    static QString getDefaultChainName() { return QString("main"); }

    static const QString labelRegExp;

    static const int     updateSpeed         = 30 * 1000;        // 30 sec
    static const int     priceRefreshSpeed   = 60 * 60 * 1000;   // 1 hr

private:
    // This class can only be accessed through Settings::getInstance()
    Settings() = default;
    ~Settings() = default;

    static Settings* instance;

    QString _executable;
    bool    _isTestnet        = false;
    bool    _isSyncing        = false;
    int     _blockNumber      = 0;
    QString _zcashdVersion    = 0;
    bool    _useEmbedded      = false;
    bool    _headless         = false;
    
    double  zecPrice          = 0.0;
    // get fancier once we have moar exchanges.. a map?
    ARWPriceInfo safetradePriceInfo;
};


inline bool isJsonResultSuccess(const json& res) {
    return res.find("result") != res.end() && 
                    QString::fromStdString(res["result"].get<json::string_t>()) == "success";
}

inline bool isJsonError(const json& res) {
    return res.find("error") != res.end();
}


#endif // SETTINGS_H
