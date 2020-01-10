#include "controller.h"

#include "addressbook.h"
#include "settings.h"
#include "version.h"
#include "camount.h"
#include "websockets.h"

using json = nlohmann::json;

Controller::Controller(MainWindow* main) {
    auto cl = new ConnectionLoader(main, this);

    // Execute the load connection async, so we can set up the rest of RPC properly. 
    QTimer::singleShot(1, [=]() { cl->loadConnection(); });

    this->main = main;
    this->ui = main->ui;

    // Setup balances table model
    balancesTableModel = new BalancesTableModel(main->ui->balancesTable);
    main->ui->balancesTable->setModel(balancesTableModel);

    // Setup transactions table model
    transactionsTableModel = new TxTableModel(ui->transactionsTable);
    main->ui->transactionsTable->setModel(transactionsTableModel);
    
    // Set up timer to refresh Price
    priceTimer = new QTimer(main);
    QObject::connect(priceTimer, &QTimer::timeout, [=]() {
        if (Settings::getInstance()->getAllowFetchPrices())
            refreshARWPrice();
    });
    priceTimer->start(Settings::priceRefreshSpeed);  // Every hour

    // Set up a timer to refresh the UI every few seconds
    timer = new QTimer(main);
    QObject::connect(timer, &QTimer::timeout, [=]() {
        refresh();
    });
    timer->start(Settings::updateSpeed);    

    // Create the data model
    model = new DataModel();

    // Crate the ZcashdRPC 
    zrpc = new LiteInterface();
}

Controller::~Controller() {
    delete timer;
    delete txTimer;

    delete transactionsTableModel;
    delete balancesTableModel;

    delete model;
    delete zrpc;
}


// Called when a connection to zcashd is available. 
void Controller::setConnection(Connection* c) {
    if (c == nullptr) return;

    this->zrpc->setConnection(c);

    ui->statusBar->showMessage("Ready!");

    processInfo(c->getInfo());

    refreshARWPrice();

    // If we're allowed to check for updates, check for a new release
    if (Settings::getInstance()->getCheckForUpdates())
        checkForUpdate();

    // Force update, because this might be coming from a settings update
    // where we need to immediately refresh
    refresh(true);
}


// Build the RPC JSON Parameters for this tx
void Controller::fillTxJsonParams(json& allRecepients, Tx tx) {   
    Q_ASSERT(allRecepients.is_array());

    // For each addr/amt/memo, construct the JSON and also build the confirm dialog box    
    for (int i=0; i < tx.toAddrs.size(); i++) {
        auto toAddr = tx.toAddrs[i];

        // Construct the JSON params
        json rec = json::object();
        rec["address"]      = toAddr.addr.toStdString();
        rec["amount"]       = toAddr.amount.toqint64();
        if (Settings::isZAddress(toAddr.addr) && !toAddr.memo.trimmed().isEmpty())
            rec["memo"]     = toAddr.memo.toStdString();

        allRecepients.push_back(rec);
    }
}


void Controller::noConnection() {    
    QIcon i = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    main->statusIcon->setPixmap(i.pixmap(16, 16));
    main->statusIcon->setToolTip("");
    main->statusLabel->setText(QObject::tr("No Connection"));
    main->statusLabel->setToolTip("");
    main->ui->statusBar->showMessage(QObject::tr("No Connection"), 1000);

    // Clear balances table.
    QMap<QString, CAmount> emptyBalances;
    QList<UnspentOutput>  emptyOutputs;
    QList<QString>        emptyAddresses;
    balancesTableModel->setNewData(emptyAddresses, emptyAddresses, emptyBalances, emptyOutputs);

    // Clear Transactions table.
    QList<TransactionItem> emptyTxs;
    transactionsTableModel->replaceData(emptyTxs);

    // Clear balances
    ui->balTotalARW->setText("");
    ui->balTotalARW->setToolTip("");
}

/// This will refresh all the balance data from zcashd
void Controller::refresh(bool force) {
    if (!zrpc->haveConnection()) 
        return noConnection();

    getInfoThenRefresh(force);
}

void Controller::processInfo(const json& info) {
    // Testnet?
    QString chainName;
    if (!info["chain_name"].is_null()) {
        chainName = QString::fromStdString(info["chain_name"].get<json::string_t>());
        Settings::getInstance()->setTestnet(chainName == "test");
    };


    QString version = QString::fromStdString(info["version"].get<json::string_t>());
    Settings::getInstance()->setZcashdVersion(version);

    // Recurring pamynets are testnet only
    if (!Settings::getInstance()->isTestnet())
        main->disableRecurring();
}

void Controller::getInfoThenRefresh(bool force) {
    if (!zrpc->haveConnection()) 
        return noConnection();

    static bool prevCallSucceeded = false;

    zrpc->fetchLatestBlock([=] (const json& reply) {   
        prevCallSucceeded = true;       

        int curBlock  = reply["height"].get<json::number_integer_t>();
        bool doUpdate = force || (model->getLatestBlock() != curBlock);
        model->setLatestBlock(curBlock);

        main->logger->write(QString("Refresh. curblock ") % QString::number(curBlock) % ", update=" % (doUpdate ? "true" : "false") );

        // Connected, so display checkmark.
        auto tooltip = Settings::getInstance()->getSettings().server + "\n" + 
                            QString::fromStdString(zrpc->getConnection()->getInfo().dump());
        QIcon i(":/icons/res/connected.gif");
        QString chainName = Settings::getInstance()->isTestnet() ? "test" : "main";
        main->statusLabel->setText(chainName + "(" + QString::number(curBlock) + ")");
        main->statusLabel->setToolTip(tooltip);
        main->statusIcon->setPixmap(i.pixmap(16, 16));
        main->statusIcon->setToolTip(tooltip);

        // See if recurring payments needs anything
        Recurring::getInstance()->processPending(main);

        // Check if the wallet is locked/encrypted
        zrpc->fetchWalletEncryptionStatus([=] (const json& reply) {
            bool isEncrypted = reply["encrypted"].get<json::boolean_t>();
            bool isLocked = reply["locked"].get<json::boolean_t>();

            model->setEncryptionStatus(isEncrypted, isLocked);
        });

        if ( doUpdate ) {
            // Something changed, so refresh everything.
            refreshBalances();        
            refreshAddresses();     // This calls refreshZSentTransactions() and refreshReceivedZTrans()
            refreshTransactions();
        }
    }, [=](QString err) {
        // zcashd has probably disappeared.
        this->noConnection();

        // Prevent multiple dialog boxes, because these are called async
        static bool shown = false;
        if (!shown && prevCallSucceeded) { // show error only first time
            shown = true;
            QMessageBox::critical(main, QObject::tr("Connection Error"), QObject::tr("There was an error connecting to arrowd. The error was") + ": \n\n"
                + err, QMessageBox::StandardButton::Ok);
            shown = false;
        }

        prevCallSucceeded = false;
    });
}

void Controller::refreshAddresses() {
    if (!zrpc->haveConnection()) 
        return noConnection();
    
    auto newzaddresses = new QList<QString>();
    auto newtaddresses = new QList<QString>();

    zrpc->fetchAddresses([=] (json reply) {
        auto zaddrs = reply["z_addresses"].get<json::array_t>();
        for (auto& it : zaddrs) {   
            auto addr = QString::fromStdString(it.get<json::string_t>());
            newzaddresses->push_back(addr);
        }

        model->replaceZaddresses(newzaddresses);

        auto taddrs = reply["t_addresses"].get<json::array_t>();
        for (auto& it : taddrs) {   
            auto addr = QString::fromStdString(it.get<json::string_t>());
            if (Settings::isTAddress(addr))
                newtaddresses->push_back(addr);
        }

        model->replaceTaddresses(newtaddresses);

        // Refresh the sent and received txs from all these z-addresses
        refreshTransactions();
    });
    
}

// Function to create the data model and update the views, used below.
void Controller::updateUI(bool anyUnconfirmed) {    
    ui->unconfirmedWarning->setVisible(anyUnconfirmed);

    // Update balances model data, which will update the table too
    balancesTableModel->setNewData(model->getAllZAddresses(), model->getAllTAddresses(), model->getAllBalances(), model->getUTXOs());
};

// Function to process reply of the listunspent and z_listunspent API calls, used below.
void Controller::processUnspent(const json& reply, QMap<QString, CAmount>* balancesMap, QList<UnspentOutput>* unspentOutputs) {
    auto processFn = [=](const json& array) {        
        for (auto& it : array) {
            QString qsAddr  = QString::fromStdString(it["address"]);
            int block       = it["created_in_block"].get<json::number_unsigned_t>();
            QString txid    = QString::fromStdString(it["created_in_txid"]);
            CAmount amount  = CAmount::fromqint64(it["value"].get<json::number_unsigned_t>());

            bool spendable = it["unconfirmed_spent"].is_null() && it["spent"].is_null();    // TODO: Wait for 4 confirmations
            bool pending   = !it["unconfirmed_spent"].is_null();

            unspentOutputs->push_back(UnspentOutput{ qsAddr, txid, amount, block, spendable, pending });
            if (spendable) {
                (*balancesMap)[qsAddr] = (*balancesMap)[qsAddr] +
                                         CAmount::fromqint64(it["value"].get<json::number_unsigned_t>());
            }
        }
    };

    processFn(reply["unspent_notes"].get<json::array_t>());
    processFn(reply["utxos"].get<json::array_t>());
    processFn(reply["pending_notes"].get<json::array_t>());
    processFn(reply["pending_utxos"].get<json::array_t>());
};

void Controller::updateUIBalances() {
    refreshARWPrice();

    CAmount balZ = getModel()->getBalZ();
    CAmount balVerified = getModel()->getBalVerified();

    // Reduce the BalanceZ by the pending outgoing amount. We're adding
    // here because totalPending is already negative for outgoing txns.
    balZ = balZ + getModel()->getTotalPending();

    CAmount balTotalARW     = balZ; //testing + CAmount::fromDecimalString("1000000.0");
    CAmount balAvailable = balZ + balVerified;

    // Balances table
    ui->balSheilded   ->setText(balZ.toDecimalZECString());
    ui->balVerified   ->setText(balVerified.toDecimalZECString());
    ui->balTotalARW      ->setText(balTotalARW.toDecimalZECString());

    ui->balSheilded   ->setToolTip(balZ.toDecimalUSDString());
    ui->balVerified   ->setToolTip(balVerified.toDecimalUSDString());

    const ARWPriceInfo& api = Settings::getInstance()->getARWPriceInfo();

    if (api.arwusd() != 0.0) {
        QString sourceTooltip("");
        QTextStream(&sourceTooltip) << "BTC(Coinmarketcap): " << api.btcusd
                                    << " ARW price: (Safetrade): " << api.arwbtc
                                    << " == " << api.arwusd();

        ui->balTotalUSD      ->setToolTip(sourceTooltip);
        ui->balTotalUSD      ->setText(balTotalARW.toDecimalUSDString());
    } else {
        QString unavailable("Unavailable");
        QString tt("Either CoinMarketCap or Safetrade is unreachable, ARWUSD unavailable.");
        ui->balTotalUSD      ->setToolTip(tt);
        ui->balTotalUSD      ->setText(unavailable);
    }

    // Send tab
    ui->txtAvailableZEC->setText(balAvailable.toDecimalZECString());
    ui->txtAvailableUSD->setText(balAvailable.toDecimalUSDString());
}

void Controller::refreshBalances() {    
    if (!zrpc->haveConnection()) 
        return noConnection();

    // 1. Get the Balances
    zrpc->fetchBalance([=] (json reply) {    
        CAmount balZ        = CAmount::fromqint64(reply["zbalance"].get<json::number_unsigned_t>());
        CAmount balVerified = CAmount::fromqint64(reply["verified_zbalance"].get<json::number_unsigned_t>());
        
        model->setBalZ(balZ);
        model->setBalVerified(balVerified);

        // This is for the websockets
        AppDataModel::getInstance()->setBalances(balZ);
        
        // This is for the datamodel
        CAmount balAvailable = balZ + balVerified;
        model->setAvailableBalance(balAvailable);

        updateUIBalances();
    });

    // 2. Get the UTXOs
    // First, create a new UTXO list. It will be replacing the existing list when everything is processed.
    auto newUnspentOutputs = new QList<UnspentOutput>();
    auto newBalances = new QMap<QString, CAmount>();

    // Call the Transparent and Z unspent APIs serially and then, once they're done, update the UI
    zrpc->fetchUnspent([=] (json reply) {
        processUnspent(reply, newBalances, newUnspentOutputs);

        // Swap out the balances and UTXOs
        model->replaceBalances(newBalances);
        model->replaceUTXOs(newUnspentOutputs);

        // Find if any output is not spendable or is pending
        bool anyUnconfirmed = std::find_if(newUnspentOutputs->constBegin(), newUnspentOutputs->constEnd(), 
                                    [=](const UnspentOutput& u) -> bool { 
                                        return !u.spendable ||  u.pending; 
                              }) != newUnspentOutputs->constEnd();

        updateUI(anyUnconfirmed);

        main->balancesReady();
    });
}

void Controller::refreshTransactions() {    
    if (!zrpc->haveConnection()) 
        return noConnection();

    zrpc->fetchTransactions([=] (json reply) {
        QList<TransactionItem> txdata;        

        for (auto& it : reply.get<json::array_t>()) {  
            QString address;
            CAmount total_amount;
            QList<TransactionItemDetail> items;

            long confirmations;
            if (it.find("unconfirmed") != it.end() && it["unconfirmed"].get<json::boolean_t>()) {
                confirmations = 0;
            } else {
                confirmations = model->getLatestBlock() - it["block_height"].get<json::number_integer_t>() + 1;
            }
            
            auto txid = QString::fromStdString(it["txid"]);
            auto datetime = it["datetime"].get<json::number_integer_t>();

            // First, check if there's outgoing metadata
            if (!it["outgoing_metadata"].is_null()) {
            
                for (auto o: it["outgoing_metadata"].get<json::array_t>()) {
                    QString address = QString::fromStdString(o["address"]);
                    
                    // Sent items are -ve
                    CAmount amount = CAmount::fromqint64(-1 * o["value"].get<json::number_unsigned_t>()); 
                    
                    QString memo;
                    if (!o["memo"].is_null()) {
                        memo = QString::fromStdString(o["memo"]);
                    }

                    items.push_back(TransactionItemDetail{address, amount, memo});
                    total_amount = total_amount + amount;
                }

                {
                    // Concat all the addresses
                    QList<QString> addresses;
                    for (auto item : items) {
                        addresses.push_back(item.address);
                    }
                    address = addresses.join(",");
                }

                txdata.push_back(TransactionItem{
                   "Sent", datetime, address, txid,confirmations, items
                });
            } else {
                // Incoming Transaction
                address = (it["address"].is_null() ? "" : QString::fromStdString(it["address"]));
                model->markAddressUsed(address);

                QString memo;
                if (!it["memo"].is_null()) {
                    memo = QString::fromStdString(it["memo"]);
                }

                items.push_back(TransactionItemDetail{
                    address,
                    CAmount::fromqint64(it["amount"].get<json::number_integer_t>()),
                    memo
                });

                TransactionItem tx{
                    "Receive", datetime, address, txid,confirmations, items
                };

                txdata.push_back(tx);
            }
            
        }

        // Calculate the total unspent amount that's pending. This will need to be 
        // shown in the UI so the user can keep track of pending funds
        CAmount totalPending;
        for (auto txitem : txdata) {
            if (txitem.confirmations == 0) {
                for (auto item: txitem.items) {
                    totalPending = totalPending + item.amount;
                }
            }
        }
        getModel()->setTotalPending(totalPending);

        // Update UI Balance
        updateUIBalances();

        // Update model data, which updates the table view
        transactionsTableModel->replaceData(txdata);        
    });
}

// If the wallet is encrpyted and locked, we need to unlock it 
void Controller::unlockIfEncrypted(std::function<void(void)> cb, std::function<void(void)> error) {
    auto encStatus = getModel()->getEncryptionStatus();
    if (encStatus.first && encStatus.second) {
        // Wallet is encrypted and locked. Ask for the password and unlock.
        QString password = QInputDialog::getText(main, main->tr("Wallet Password"), 
                            main->tr("Your wallet is encrypted.\nPlease enter your wallet password"), QLineEdit::Password);

        if (password.isEmpty()) {
            QMessageBox::critical(main, main->tr("Wallet Decryption Failed"),
                main->tr("Please enter a valid password"),
                QMessageBox::Ok
            );
            error();
            return;
        }

        zrpc->unlockWallet(password, [=](json reply) {
            if (isJsonResultSuccess(reply)) {
                cb();

                // Refresh the wallet so the encryption status is now in sync.
                refresh(true);
            } else {
                QMessageBox::critical(main, main->tr("Wallet Decryption Failed"),
                    QString::fromStdString(reply["error"].get<json::string_t>()),
                    QMessageBox::Ok
                );
                error();
            }
        });
    } else {
        // Not locked, so just call the function
        cb();
    }
}

/**
 * Execute a transaction with the standard UI. i.e., standard status bar message and standard error
 * handling
 */
void Controller::executeStandardUITransaction(Tx tx) {
    executeTransaction(tx,
        [=] (QString txid) { 
            ui->statusBar->showMessage(Settings::txidStatusMessage + " " + txid);
        },
        [=] (QString opid, QString errStr) {
            ui->statusBar->showMessage(QObject::tr(" Tx ") % opid % QObject::tr(" failed"), 15 * 1000);

            if (!opid.isEmpty())
                errStr = QObject::tr("The transaction with id ") % opid % QObject::tr(" failed. The error was") + ":\n\n" + errStr; 

            QMessageBox::critical(main, QObject::tr("Transaction Error"), errStr, QMessageBox::Ok);            
        }
    );
}


// Execute a transaction!
void Controller::executeTransaction(Tx tx, 
        const std::function<void(QString txid)> submitted,
        const std::function<void(QString txid, QString errStr)> error) {
    unlockIfEncrypted([=] () {
        // First, create the json params
        json params = json::array();
        fillTxJsonParams(params, tx);
        std::cout << std::setw(2) << params << std::endl;

        zrpc->sendTransaction(QString::fromStdString(params.dump()), [=](const json& reply) {
            if (reply.find("txid") == reply.end()) {
                error("", "Couldn't understand Response: " + QString::fromStdString(reply.dump()));
            } else {
                QString txid = QString::fromStdString(reply["txid"].get<json::string_t>());
                submitted(txid);
            }
        },
        [=](QString errStr) {
            error("", errStr);
        });
    }, [=]() {
        error("", main->tr("Failed to unlock wallet"));
    });
}


void Controller::checkForUpdate(bool silent) {
    if (!zrpc->haveConnection()) 
        return noConnection();

    QUrl cmcURL("https://api.github.com/repos/Arrowchain/quiver-lite/releases");

    QNetworkRequest req;
    req.setUrl(cmcURL);
    
    QNetworkAccessManager *manager = new QNetworkAccessManager(this->main);
    QNetworkReply *reply = manager->get(req);

    QObject::connect(reply, &QNetworkReply::finished, [=] {
        reply->deleteLater();
        manager->deleteLater();

        try {
            if (reply->error() == QNetworkReply::NoError) {

                auto releases = QJsonDocument::fromJson(reply->readAll()).array();
                QVersionNumber maxVersion(0, 0, 0);

                for (QJsonValue rel : releases) {
                    if (!rel.toObject().contains("tag_name"))
                        continue;

                    QString tag = rel.toObject()["tag_name"].toString();
                    if (tag.startsWith("v"))
                        tag = tag.right(tag.length() - 1);

                    if (!tag.isEmpty()) {
                        auto v = QVersionNumber::fromString(tag);
                        if (v > maxVersion)
                            maxVersion = v;
                    }
                }

                auto currentVersion = QVersionNumber::fromString(APP_VERSION);
                
                // Get the max version that the user has hidden updates for
                QSettings s;
                auto maxHiddenVersion = QVersionNumber::fromString(s.value("update/lastversion", "0.0.0").toString());

                qDebug() << "Version check: Current " << currentVersion << ", Available " << maxVersion;

                if (maxVersion > currentVersion && (!silent || maxVersion > maxHiddenVersion)) {
                    auto ans = QMessageBox::information(main, QObject::tr("Update Available"), 
                        QObject::tr("A new release v%1 is available! You have v%2.\n\nWould you like to visit the releases page?")
                            .arg(maxVersion.toString())
                            .arg(currentVersion.toString()),
                        QMessageBox::Yes, QMessageBox::Cancel);
                    if (ans == QMessageBox::Yes) {
                        QDesktopServices::openUrl(QUrl("https://github.com/Arrowchain/quiver-lite/releases"));
                    } else {
                        // If the user selects cancel, don't bother them again for this version
                        s.setValue("update/lastversion", maxVersion.toString());
                    }
                } else {
                    if (!silent) {
                        QMessageBox::information(main, QObject::tr("No updates available"), 
                            QObject::tr("You already have the latest release v%1")
                                .arg(currentVersion.toString()));
                    }
                } 
            }
        }
        catch (...) {
            // If anything at all goes wrong, just set the price to 0 and move on.
            qDebug() << QString("Caught something nasty");
        }       
    });
}

// if the site is not available, fails quickly -- i dont understand why bother with the async crap.
// todo: promote this to a common header for broader use
static QByteArray sync_get_the_page_already(const QString& url)
{
    // i love how QT manages to make literally everything take 100x as much effort as necessary.
    // can we please use python instead?
    QNetworkAccessManager NAManager;
    //QUrl url ("http://www.google.com");
    QNetworkRequest request(url);
    QNetworkReply *reply = NAManager.get(request);
    QEventLoop eventLoop;
    QObject::connect(reply, SIGNAL(finished()), &eventLoop, SLOT(quit()));
    eventLoop.exec();
    QByteArray raw = reply->readAll();
    // json likes it raw. very well, precious.
    //QString response = QTextCodec::codecForMib(106)->toUnicode(raw);
    return raw;
}


void Controller::refreshARWPrice()
{
    //overall algo:
    // get last price for:
    // safetrade ARWBTC
    // cmc BTCUSD
    // compute ARWUSD
    // todo: as we have more exchanges, move to a config driven model
    // where we have several default exchanges and users can select which ones they
    // want to use for their pricing data (aids early arbitrage/sanity checks prices)
    // then remove later and just use CMC once pricing is reliably low spread.

    QString cmcURL("https://api.coinmarketcap.com/v1/ticker/");
    QString safetradeURL("https://safe.trade/api/v2/peatio/public/markets/tickers");

    ARWPriceInfo api; api.e = Exchange::SAFETRADE;

    QByteArray cmc;
    QByteArray safetrade;

    try {
        cmc = sync_get_the_page_already(cmcURL);
        safetrade = sync_get_the_page_already(safetradeURL);
    } catch(...) {
        QString err("ERROR: unable to reach either coinmarketcap or safetrade, pricing info will be missing till success.");
        return;
    }

    try {

        auto parsedCMC = json::parse(cmc, nullptr, false);
        auto parsedSafetrade = json::parse(safetrade, nullptr, false);

        auto cmc_coin_array = parsedCMC.get<json::array_t>();

        for(auto coin : cmc_coin_array) {
            auto sym = coin["symbol"].get<json::string_t>();
            if (sym == "BTC") {
                auto usd_as_str = coin["price_usd"].get<json::string_t>();
                api.btcusd = std::stod(usd_as_str);
                break;// for
            }
        }

        auto st_tick = parsedSafetrade["arwbtc"]["ticker"];
        auto st_vol = st_tick["volume"].get<json::string_t>();
        auto st_avp = st_tick["avg_price"].get<json::string_t>();
        auto st_pcp = st_tick["price_change_percent"].get<json::string_t>();

        api.arwbtc = std::stod(st_avp);
        api.volume = std::stod(st_vol);
        api.price_change_percent = std::stod(st_pcp);

        main->logger->write(QString("refreshARWPrice pulled: ") % api.to_string());
    } catch (...) {
        QString err("ERROR: had trouble parsing coinmarketcap (for btcusd) and/or safettrade (for arwbtc).  HTTP responses follow.");
        main->logger->write(err);
        main->logger->write(cmc);
        main->logger->write(safetrade);
        return;
    }

    // will zero out the price info for safetrade if we fail.
    // refactor this a bit when we get moar exchanges.
    // maybe roundrobin pull every couple seconds from whatever N exchanges
    // are of interest to the user.
    Settings::getInstance()->setARWPriceInfo(api);
}

void Controller::shutdownZcashd() {
    // Save the wallet and exit the lightclient library cleanly.
    if (zrpc->haveConnection()) {
        QDialog d(main);
        Ui_ConnectionDialog connD;
        connD.setupUi(&d);
        connD.topIcon->setBasePixmap(QIcon(":/icons/res/icon.ico").pixmap(256, 256));
        connD.status->setText(QObject::tr("Please wait for Quiver to exit"));
        connD.statusDetail->setText(QObject::tr("Waiting for arrowd to exit"));

        bool finished = false;

        zrpc->saveWallet([&] (json) {        
            if (!finished)
                d.accept();
            finished = true;
        });

        if (!finished)
            d.exec();
    }
}

/** 
 * Get a Sapling address from the user's wallet
 */ 
QString Controller::getDefaultSaplingAddress() {
    for (QString addr: model->getAllZAddresses()) {
        if (Settings::getInstance()->isSaplingAddress(addr))
            return addr;
    }

    return QString();
}

QString Controller::getDefaultTAddress() {
    if (model->getAllTAddresses().length() > 0)
        return model->getAllTAddresses().at(0);
    else 
        return QString();
}
