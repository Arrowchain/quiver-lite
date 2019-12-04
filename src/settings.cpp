#include "mainwindow.h"
#include "settings.h"
#include "camount.h"

Settings* Settings::instance = nullptr;

Settings* Settings::init() {
    if (instance == nullptr)
        instance = new Settings();

    return instance;
}

Settings* Settings::getInstance() {
    return instance;
}

Config Settings::getSettings() {
    // Load from the QT Settings.
    QSettings s;

    auto server        = s.value("connection/server").toString();
    if (server.trimmed().isEmpty()) {
        server = Settings::getDefaultServer();
    }

    return Config{server};
}

void Settings::saveSettings(const QString& server) {
    QSettings s;

    s.setValue("connection/server", server);
    s.sync();

    // re-init to load correct settings
    init();
}

bool Settings::isTestnet() {
    return _isTestnet;
}

void Settings::setTestnet(bool isTestnet) {
    this->_isTestnet = isTestnet;
}

bool Settings::isSaplingAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return ( isTestnet() && addr.startsWith("ztestsapling")) ||
           (!isTestnet() && addr.startsWith("as"));
}

bool Settings::isSproutAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return isZAddress(addr) && !isSaplingAddress(addr);
}

bool Settings::isZAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return addr.startsWith("as");
}

bool Settings::isTAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return addr.startsWith("ar");
}

QString Settings::getZcashdVersion() {
    return _zcashdVersion;
}

void Settings::setZcashdVersion(QString version) {
    _zcashdVersion = version;
}

bool Settings::isSyncing() {
    return _isSyncing;
}

void Settings::setSyncing(bool syncing) {
    this->_isSyncing = syncing;
}

int Settings::getBlockNumber() {
    return this->_blockNumber;
}

void Settings::setBlockNumber(int number) {
    this->_blockNumber = number;
}

bool Settings::isSaplingActive() {
    return  (isTestnet() && getBlockNumber() > 280000) ||
           (!isTestnet() && getBlockNumber() > 419200);
}

double Settings::getZECPrice() {
    return zecPrice;
}

bool Settings::getCheckForUpdates() {
    return QSettings().value("options/allowcheckupdates", true).toBool();
}

void Settings::setCheckForUpdates(bool allow) {
     QSettings().setValue("options/allowcheckupdates", allow);
}

bool Settings::getAllowFetchPrices() {
    return QSettings().value("options/allowfetchprices", true).toBool();
}

void Settings::setAllowFetchPrices(bool allow) {
     QSettings().setValue("options/allowfetchprices", allow);
}

QString Settings::get_theme_name() {
    // Load from the QT Settings.
    return QSettings().value("options/theme_name", false).toString();
}

void Settings::set_theme_name(QString theme_name) {
    QSettings().setValue("options/theme_name", theme_name);
}


//=================================
// Static Stuff
//=================================
void Settings::saveRestore(QDialog* d) {
    d->restoreGeometry(QSettings().value(d->objectName() % "geometry").toByteArray());

    QObject::connect(d, &QDialog::finished, [=](auto) {
        QSettings().setValue(d->objectName() % "geometry", d->saveGeometry());
    });
}

void Settings::saveRestoreTableHeader(QTableView* table, QDialog* d, QString tablename) {
    table->horizontalHeader()->restoreState(QSettings().value(tablename).toByteArray());
    table->horizontalHeader()->setStretchLastSection(true);

    QObject::connect(d, &QDialog::finished, [=](auto) {
        QSettings().setValue(tablename, table->horizontalHeader()->saveState());
    });
}

QString Settings::getDefaultServer() {
    return "https://18.204.209.173:443/";
}

void Settings::openAddressInExplorer(QString address) {
    QString url;
    if (Settings::getInstance()->isTestnet()) {
        url = "https://chain.so/address/ZECTEST/" + address;
    } else {
        url = "https://zcha.in/accounts/" + address;
    }
    QDesktopServices::openUrl(QUrl(url));
}

void Settings::openTxInExplorer(QString txid) {
    QString url;
    if (Settings::getInstance()->isTestnet()) {
        url = "https://chain.so/tx/ZECTEST/" + txid;
    }
    else {
        url = "https://zcha.in/transactions/" + txid;
    }
    QDesktopServices::openUrl(QUrl(url));
}


const QString Settings::txidStatusMessage = QString(QObject::tr("Tx submitted (right click to copy) txid:"));

QString Settings::getTokenName() {
    if (Settings::getInstance()->isTestnet()) {
        return "TAR";
    } else {
        return "ARW";
    }
}

QString Settings::getDonationAddr() {
    if (Settings::getInstance()->isTestnet())
            return "ztestsapling1wn6889vznyu42wzmkakl2effhllhpe4azhu696edg2x6me4kfsnmqwpglaxzs7tmqsq7kudemp5";
    else
            return "zs1gv64eu0v2wx7raxqxlmj354y9ycznwaau9kduljzczxztvs4qcl00kn2sjxtejvrxnkucw5xx9u";

}

CAmount Settings::getMinerFee() {
    return CAmount::fromqint64(10000);
}

bool Settings::isValidSaplingPrivateKey(QString pk) {
    if (isTestnet()) {
        QRegExp zspkey("^secret-extended-key-test[0-9a-z]{278}$", Qt::CaseInsensitive);
        return zspkey.exactMatch(pk);
    } else {
        QRegExp zspkey("^secret-extended-key-main[0-9a-z]{278}$", Qt::CaseInsensitive);
        return zspkey.exactMatch(pk);
    }
}

bool Settings::isValidAddress(QString addr) {
    QRegExp zcexp("^as[a-z0-9]{94}$",  Qt::CaseInsensitive);
    QRegExp zsexp("^as[a-z0-9]{76}$",  Qt::CaseInsensitive);
    QRegExp ztsexp("^ztestsapling[a-z0-9]{76}", Qt::CaseInsensitive);
    QRegExp texp("^ar[a-z0-9]{34}$", Qt::CaseInsensitive);

    return  zcexp.exactMatch(addr)  || texp.exactMatch(addr) ||
            ztsexp.exactMatch(addr) || zsexp.exactMatch(addr);
}

// Get a pretty string representation of this Payment URI
QString Settings::paymentURIPretty(PaymentURI uri) {
    CAmount amount = CAmount::fromDecimalString(uri.amt);
    return QString() + "Payment Request\n" + "Pay: " + uri.addr + "\nAmount: " + amount.toDecimalZECString()
        + "\nMemo:" + QUrl::fromPercentEncoding(uri.memo.toUtf8());
}

// Parse a payment URI string into its components
PaymentURI Settings::parseURI(QString uri) {
    PaymentURI ans;

    if (!uri.startsWith("arrow:")) {
        ans.error = "Not a arrow payment URI";
        return ans;
    }

    uri = uri.right(uri.length() - QString("arrow:").length());

    QRegExp re("([a-zA-Z0-9]+)");
    int pos;
    if ( (pos = re.indexIn(uri)) == -1 ) {
        ans.error = "Couldn't find an address";
        return ans;
    }

    ans.addr = re.cap(1);
    if (!Settings::isValidAddress(ans.addr)) {
        ans.error = "Could not understand address";
        return ans;
    }
    uri = uri.right(uri.length() - ans.addr.length());

    if (!uri.isEmpty()) {
        uri = uri.right(uri.length() - 1); // Eat the "?"

        QStringList args = uri.split("&");
        for (QString arg: args) {
            QStringList kv = arg.split("=");
            if (kv.length() != 2) {
                ans.error = "No value argument was seen";
                return ans;
            }

            if (kv[0].toLower() == "amt" || kv[0].toLower() == "amount") {
                ans.amt = kv[1];
            } else if (kv[0].toLower() == "memo" || kv[0].toLower() == "message" || kv[0].toLower() == "msg") {
                ans.memo = QUrl::fromPercentEncoding(kv[1].toUtf8());
            } else {
                // Ignore unknown fields, since some developers use it to pass extra data.
            }
        }
    }

    return ans;
}

const QString Settings::labelRegExp("[a-zA-Z0-9\\-_]{0,40}");
