Quiver Lite is the official lite wallet for Arrow. It is z-Addr first and Sapling-compatible, and it has full support for all Arrow features:

	* Fully shielded transactions
	* Transparent addresses and transactions
	* Incoming and outgoing memos
	* Full encryption for your private keys using viewkeys to sync the blockchain

## Installation

The following instructions explain how to install Quiver Lite by using installers.

### macOS

1. [Download the latest release](https://github.com/Arrowchain/quiver-lite/releases).

1. Double-click the `.dmg` file, and drag the Quiver icon into the Applications folder.

### Windows

1. [Download the latest release](https://github.com/Arrowchain/quiver-lite/releases).

1. Extract the contents of the `.zip` file, and drag the Quiver icon into the Applications folder.

### Linux

If you use the Debian or Ubuntu operating system, [use the `.deb` package](#use-the-deb-package). Otherwise, [run the binaries](#run-the-binaries).

#### Use the `.deb` package

Run the following commands in a terminal window:

```
sudo dpkg -i linux-quiver-lite-v1.0.2.deb
sudo apt install -f
```

#### Run the binaries

Run the following commands in a terminal window:

```
tar -xvf linux-quiver-lite-v1.0.2.tar.gz
./quiver-lite-v1.0.2/quiver-lite
```

### Compiling from source

Quiver Lite is written in C++ 14. Compile it with g++/clang++/visual c++. 

#### Prerequisites

	* g++, clang++, or Visual C++
	* [Qt5](https://www.qt.io/download).
	* Rust v1.37 or later releases

#### Building on Linux

```
git clone https://github.com/Arrowchain/quiver-lite.git
cd quiver-lite
/path/to/qt5/bin/qmake quiver-lite.pro CONFIG+=debug
make -j$(nproc)

./quiver-lite
```

## Privacy

Although all the keys and transaction detection happens on the client, the server can determine which blocks contain your shielded transactions and other metadata about you, such as your IP address.

## Note management

Quiver Lite does automatic note and UTXO management. That is, it doesn't allow you to manually select the address from which to send outgoing transactions. It follows these conventions:

	* Defaults to sending shielded transactions, even if you're sending ARW to a transparent address
	* Sapling funds need at least five confirmations before they can be spent
	* Can select funds from multiple shielded addresses in the same transaction
	* Automatically shields your transparent funds at the first opportunity
    * When sending an outgoing transaction to a shielded address, Quiver Lite uses the transaction to also shield your transparent funds (i.e., sends your transparent funds to your own shielded address in the same transaction)

## Troubleshooting

For support, join the [Arrow community on Discord](https://discord.gg/RdcrR9P) or send a tweet to [@ArrowCrypto](http://www.twitter.com/arrowcrypto).