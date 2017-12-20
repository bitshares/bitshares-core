# TravelChain quickstart guide for Ubuntu 16.04

## Prerequisites

Let's start with clear dedicated server like this:
```
~# ip a show enp2s0 | grep "inet "
inet 1.1.1.256/20 brd 1.1.1.255 scope global enp2s0

~# cat /etc/issue
Ubuntu 16.04.3 LTS \n \l

~# grep processor -c /proc/cpuinfo
8

~# free -m | grep -v Swap
              total        used        free      shared  buff/cache   available
Mem:          16036         147       13697          80        2190       15474

~# df -h /
Filesystem      Size  Used Avail Use% Mounted on
/dev/md1        211G  2.8G  198G   2% /

~# grep active /proc/mdstat
md1 : active raid1 sda3[0] sdb3[1]
md0 : active raid1 sdb2[1] sda2[0]
```

So we have a server with Ubuntu 16.04, 8 CPUs, 16GB RAM, 211GB SSD in software RAID 1 and an external IP address `1.1.1.256`.

First we need to update system and install neccessary packages:
```bash
apt update && apt -y upgrade
apt install -y cmake make libbz2-dev libdb++-dev libdb-dev libssl-dev openssl libreadline-dev autoconf libtool git ntp libcurl4-openssl-dev g++ libboost-all-dev lsof
```

Reboot system if any kernel updates has installed.

Create user `travelchain` for TravelChain running:
```bash
useradd travelchain
passwd travelchain
chsh -s /bin/bash travelchain
usermod -d /srv travelchain
chown -R travelchain:travelchain /srv/
```

Disable SSH login for `root`:
```bash
vim /etc/ssh/sshd_config
...
PermitRootLogin no
```

Restart SSH service:
```bash
systemctl restart sshd
```

Try to connect to server by SSH with user `travelchain`:
```bash
ssh-copy-id travelchain@1.1.1.256
ssh travelchain@1.1.1.256
```

## TravelChain installation

Login to system by user `travelchain`, clone repo and install it:
```bash
su - travelchain
git clone https://github.com/travelchain/travelchain-core.git --recursive
cd travelchain-core/
cmake -DCMAKE_BUILD_TYPE=Release .
make
```

## TravelChain setup

Initialize witness node:
```bash
/srv/travelchain-core/programs/witness_node/witness_node
Ctrl+C
```

Edit generated config file:
```bash
vim /srv/witness_node_data_dir/config.ini
...
# Production node WALLET.TRAVELCHAIN.IO
seed-node = 142.44.247.175:4242

# 1000 for ~5GB RAM, 3000 for ~15GB RAM
max-ops-per-account = 3000 

rpc-endpoint = 127.0.0.1:8090
enable-stale-production = true
```

Run again node with `--resync-blockchain` parameter:
```bash
/srv/travelchain-core/programs/witness_node/witness_node --data-dir /srv/witness_node_data_dir/ --resync-blockchain
```

## Wallet setup

Do not stop `witness_node` for this time.

In new terminal create wallet and set password:
```bash
/srv/travelchain-core/programs/cli_wallet/cli_wallet
new >>> set_password Your_Pa$$w0rd
Ctrl+C
```

Check if json file was created:
```bash
ls -l /srv/wallet.json
-rw------- 1 travelchain travelchain 2374 Dec 20 10:11 /srv/wallet.json
```

Return to old ssh session and stop `witness_node`.

## Systemd service file

Create systemd service file for `witness_node`:
```
~$ su - root
~# vim /lib/systemd/system/witness_node.service
[Unit]
Description=witness_node
After=network.target

[Service]
Type=simple
User=travelchain
Group=travelchain
ExecStart=/srv/travelchain-core/programs/witness_node/witness_node --data-dir /srv/witness_node_data_dir/
Restart=always

[Install]
WantedBy=multi-user.target
```

Add service to autostart and start it:
```
~# systemctl enable witness_node
~# systemctl start witness_node
```

Check port `8090`:
```
~# lsof -i :8090
COMMAND     PID        USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
witness_n 15961 travelchain   20u  IPv4 124737      0t0  TCP localhost:8090 (LISTEN)
```

## wallet.travelchain.io

1. Create new account on https://wallet.travelchain.io/

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/registration.png)

2. Keep your password in safe place

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/backup.png)

3. Get your public key on `Account - Permissions` tab

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/public_key.png)

4. Click on public key and get a private key

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/private_key.png)

## Bind your wallet to witness_node

Check if `witness_node` is working:
```bash
systemctl is-active witness_node
active
```

Run `cli_wallet` and import keys:
```bash
/srv/travelchain-core/programs/cli_wallet/cli_wallet
locked >>> unlock Your_Pa$$w0rd
unlocked >>> import_key YourTCLogin YourPrivateKey
unlocked >>> list_my_accounts
unlocked >>> create_witness YourTCLogin "https://www.your_site.com" true
unlocked >>> get_witness YourTCLogin
# Remember id value (for example 1.6.6)
unlocked >>> update_witness YourTCLogin "https://www.your_site.com" "YourPublicKey" true
```

If you want to vote for yourself (you need to have TravelTokens for this operation):
```
unlocked >>> vote_for_witness YourTCLogin YourTCLogin true true
Ctrl+C
```

Edit `witness_node` config:
```bash
vim /srv/witness_node_data_dir/config.ini
...
private-key = ["YourPublicKey", "YourPrivateKey"]
witness-id = "1.6.6" # Do you remember get_witness output?
```

And restart the `witness_node`:
```bash
su - root
systemctl restart witness_node
```

## Check yourself in Witnesses list

Go to `Account - Voting` tab

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/vote_table.png)

## Troubleshooting

* You can run `witness node` in screen session instead systemd
```bash
apt install -y screen
su - travelchain
screen -S witness_node
/srv/travelchain-core/programs/witness_node/witness_node --data-dir /srv/witness_node_data_dir/ 
Ctrl+a d
```

* Also you can check `p2p.log` in realtime
```bash
tail -f /srv/witness_node_data_dir/logs/p2p/p2p.log
```

* Do not forget to backup your wallet files
```bash
scp /srv/*.wallet backup-server:/backup_dir/
```
