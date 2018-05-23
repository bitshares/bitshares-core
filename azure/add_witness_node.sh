#!/bin/bash

set -e 

date
ps axjf

USER_NAME=$1
FQDN=$2
WITNESS_NAMES=$3
NPROC=$(nproc)
UBUNTU_VERSION=$4
if [ $UBUNTU_VERSION = "17.10" ]; then
    LOCAL_IP=`ifconfig|xargs|awk '{print $6}'|sed -e 's/[a-z]*:/''/'`
else
    LOCAL_IP=`ifconfig|xargs|awk '{print $7}'|sed -e 's/[a-z]*:/''/'`
fi
P2P_PORT=1776
RPC_PORT=8090
TLS_PORT=443
GITHUB_REPOSITORY=https://github.com/bitshares/bitshares-core.git
PROJECT=bitshares-core
BRANCH=$5
SWAP_SIZE=$6
BUILD_TYPE=Release
WITNESS_NODE=bts-witness
CLI_WALLET=bts-cli_wallet
PUBLIC_BLOCKCHAIN_SERVER=wss://bitshares.openledger.info/ws
TRUSTED_BLOCKCHAIN_DATA=https://rfxblobstorageforpublic.blob.core.windows.net/rfxcontainerforpublic/bitshares-blockchain.tar.gz

echo "USER_NAME: $USER_NAME"
echo "WITNESS_NAMES : $WITNESS_NAMES"
echo "FQDN: $FQDN"
echo "nproc: $NPROC"
echo "eth0: $LOCAL_IP"
echo "P2P_PORT: $P2P_PORT"
echo "RPC_PORT: $RPC_PORT"
echo "TLS_PORT: $TLS_PORT"
echo "GITHUB_REPOSITORY: $GITHUB_REPOSITORY"
echo "PROJECT: $PROJECT"
echo "BRANCH: $BRANCH"
echo "SWAP_SIZE: $SWAP_SIZE"
echo "BUILD_TYPE: $BUILD_TYPE"
echo "WITNESS_NODE: $WITNESS_NODE"
echo "CLI_WALLET: $CLI_WALLET"
echo "PUBLIC_BLOCKCHAIN_SERVER: $PUBLIC_BLOCKCHAIN_SERVER"
echo "TRUSTED_BLOCKCHAIN_DATA: $TRUSTED_BLOCKCHAIN_DATA"
echo "UBUNTU_VERSION: $UBUNTU_VERSION"

##################################################################################################
# Update Ubuntu, configure a swap file and install prerequisites for running BitShares.          #
##################################################################################################
sudo apt-get -y update || exit 1;
sleep 5;
SWAP_SIZE=6
fallocate -l $SWAP_SIZE'g' /mnt/$SWAP_SIZE'GiB.swap'
chmod 600 /mnt/$SWAP_SIZE'GiB.swap'
mkswap /mnt/$SWAP_SIZE'GiB.swap'
swapon /mnt/$SWAP_SIZE'GiB.swap'
echo '/mnt/'$SWAP_SIZE'GiB.swap swap swap defaults 0 0' | tee -a /etc/fstab
time apt-get -y install autoconf cmake git libboost-all-dev libssl-dev g++ libcurl4-openssl-dev

##################################################################################################
# Configure the attached datadisk using the Azure disk utilities script for Ubuntu.              #
##################################################################################################
wget https://raw.githubusercontent.com/Azure/azure-quickstart-templates/master/shared_scripts/ubuntu/vm-disk-utils-0.1.sh
bash vm-disk-utils-0.1.sh
rm vm-disk-utils-0.1.sh

##################################################################################################
# Build BitShares from source                                                                    #
##################################################################################################
cd /usr/local/src
time git clone $GITHUB_REPOSITORY
cd $PROJECT
time git checkout $BRANCH
time git submodule sync --recursive
time git submodule update --init --recursive
time cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE .
time make -j$NPROC

cp /usr/local/src/$PROJECT/programs/witness_node/witness_node /usr/bin/$WITNESS_NODE
cp /usr/local/src/$PROJECT/programs/cli_wallet/cli_wallet /usr/bin/$CLI_WALLET

##################################################################################################
# Create bitshares-core service. Enable it to start on boot.                                     #
##################################################################################################
cat >/lib/systemd/system/$PROJECT.service <<EOL
[Unit]
Description=Job that runs $PROJECT daemon
[Service]
Type=simple
Environment=statedir=/home/$USER_NAME/$PROJECT/witness_node
ExecStartPre=/bin/mkdir -p /home/$USER_NAME/$PROJECT/witness_node
ExecStart=/usr/bin/$WITNESS_NODE --data-dir /home/$USER_NAME/$PROJECT/witness_node

TimeoutSec=300
[Install]
WantedBy=multi-user.target
EOL

##################################################################################################
# Start the service, allowing it to create the default application configuration file. Stop the  #
# service to allow modification of the config.ini file.                                          #
##################################################################################################
systemctl daemon-reload
systemctl enable $PROJECT
service $PROJECT start
sleep 5; # allow time to initializize application data.
service $PROJECT stop

##################################################################################################
# Connect the local CLI Wallet to a public blockchain server and open a local RPC listener.      #
# Connect to the local CLI Wallet to generate a new key pair for use locally by the block        #
# producer. Configure the config.ini file with the new key pair and block producer identity.     #
# This key pair will be used only as the signing key on this virtual machine.                    #
# TODO: foreach name in $WITNESS_NAMES: set key pair, lookup id and update config.ini            #
# TODO: ensure graceful results if get_witness querey fails                                      #
##################################################################################################
screen -dmS $CLI_WALLET /usr/bin/$CLI_WALLET --server-rpc-endpoint=$PUBLIC_BLOCKCHAIN_SERVER --rpc-http-endpoint=127.0.0.1:8093
sleep 10; # allow time for CLI Wallet to connect to public blockchain server and open local RPC listener.
WITNESS_KEY_PAIR=$(curl -s --data '{"jsonrpc": "2.0", "method": "suggest_brain_key", "params": [], "id": 1}' http://127.0.0.1:8093 | \
    python3 -c "import sys, json; keys=json.load(sys.stdin); print('[\"'+keys['result']['pub_key']+'\",\"'+keys['result']['wif_priv_key']+'\"]')")
WITNESS_ID=$(curl -s --data '{"jsonrpc": "2.0", "method": "get_witness", "params": ["'$WITNESS_NAMES'"], "id": 1}' http://127.0.0.1:8093 | \
    python3 -c "import sys, json; print('\"'+json.load(sys.stdin)['result']['id']+'\"')")
screen -S $CLI_WALLET -p 0 -X quit

# Update the config.ini file with the new values.
sed -i 's/# witness-id =/witness-id = '$WITNESS_ID'/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/private-key =/private-key = '$WITNESS_KEY_PAIR' \nprivate-key =/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# rpc-endpoint =/rpc-endpoint = '$LOCAL_IP':'$RPC_PORT'/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# plugins =/plugins = witness/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini

##################################################################################################
# OPTIONAL: Download a recent blockchain snapshot from a trusted source. The blockchain is large #
# and will take many hours to validate using the trustless P2P network. A peer reviewed snapshot #
# is provided to facilatate rapid node deployment. Once the dowload is complete the service will #
# start and load the remaining blocks from the P2P network as normal.                            #
# TODO: add trusted source for testnet blockcahin data.                                          #
# TODO: add template option: sync from trusted source (default) or bootstrap from P2P            #
##################################################################################################
if [ "$BRANCH" = "master" ]; then
    mv /home/$USER_NAME/$PROJECT/witness_node/config.ini /home/$USER_NAME
    rm -rfv /home/$USER_NAME/$PROJECT/witness_node/*
    mv /home/$USER_NAME/config.ini /home/$USER_NAME/$PROJECT/witness_node
    cd /home/$USER_NAME/$PROJECT/witness_node
    time wget -qO- $TRUSTED_BLOCKCHAIN_DATA | tar xvz
fi

service $PROJECT start

##################################################################################################
# OPTIONAL: Install Elasticsearch to store market and account history to a local database. This  #
# reduces memory usage as operations are committed to disk and blockchain state remains in RAM.  #
# However, disk storage goes wy up. Java is a prerequisite, so it will also be installed.        #
##################################################################################################
service $PROJECT stop
add-apt-repository -y ppa:webupd8team/java
wget -qO - https://artifacts.elastic.co/GPG-KEY-elasticsearch | sudo apt-key add -
# TODO: Resolve documented issue with Elasticsearch 6.x version; Use 5.x for now. 
# echo "deb https://artifacts.elastic.co/packages/6.x/apt stable main" | sudo tee -a /etc/apt/sources.list.d/elastic-6.x.list
echo "deb https://artifacts.elastic.co/packages/5.x/apt stable main" | sudo tee -a /etc/apt/sources.list.d/elastic-5.x.list
echo "oracle-java8-installer shared/accepted-oracle-license-v1-1 select true" | sudo debconf-set-selections
apt-get update
apt -y install default-jre default-jdk oracle-java8-installer elasticsearch
systemctl daemon-reload
systemctl enable elasticsearch
# TODO: service elasticsearch start # It takes a bit of time to initialize Elasticsearch, so may need to sleep after starting

apt -y install python3-pip
pip3 install elasticsearch elasticsearch-dsl flask flask-cors
cd /usr/local/src/
git clone https://github.com/oxarbitrage/bitshares-es-wrapper.git
cd bitshares-es-wrapper
export FLASK_APP=wrapper.py
# flask run # TODO: comment the print statement in wrapper.py

pip3 install websocket-client psycopg2

##################################################################################################
# Reconfigure bitshares-core service to Want Elasticsearch. Ensure Elasticsearch starts first.   #
##################################################################################################
cat >/lib/systemd/system/$PROJECT.service <<EOL
[Unit]
Description=Job that runs $PROJECT daemon
After=elasticsearch.service
[Service]
Type=simple
Environment=statedir=/home/$USER_NAME/$PROJECT/witness_node
ExecStartPre=/bin/mkdir -p /home/$USER_NAME/$PROJECT/witness_node
ExecStart=/usr/bin/$WITNESS_NODE --data-dir /home/$USER_NAME/$PROJECT/witness_node

TimeoutSec=300
[Install]
WantedBy=multi-user.target
Wants=elasticsearch.service
EOL
systemctl daemon-reload

# Update configuration file
sed -i 's%# elasticsearch-node-url =%elasticsearch-node-url = http://localhost:9200/%g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# elasticsearch-bulk-replay =/elasticsearch-bulk-replay = 10000/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# elasticsearch-bulk-sync =/elasticsearch-bulk-sync = 100/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# elasticsearch-logs =/elasticsearch-logs = true/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/# elasticsearch-visitor =/elasticsearch-visitor = true/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
sed -i 's/plugins = witness/plugins = witness market_history elasticsearch/g' /home/$USER_NAME/$PROJECT/witness_node/config.ini
service $PROJECT start 

##################################################################################################
# OPTIONAL: Expose a secure web socket (WSS) endpoint to the public Internet. Install nginx web  #
# server to proxy RPC data over a TLS connection to the upstream web socket for the block        #
# producing node. Request a TLS certificate from Let's Encrypt, then set a cronjob for renewals. # 
##################################################################################################
apt-get -y install software-properties-common
add-apt-repository -y ppa:certbot/certbot
apt-get update
apt-get -y install nginx python-certbot-nginx 
rm /etc/nginx/sites-enabled/default
mkdir -p /var/www/$FQDN
cat >/var/www/$FQDN/index.html <<EOL
<html>
  <head>
    <title>$PROJECT API</title>
  </head>
  <body>
    <h1>$PROJECT API</h1>
  </body>
</html>
EOL

cat >/etc/nginx/sites-available/$FQDN <<EOL
upstream websockets {
    server $LOCAL_IP:$RPC_PORT;
}
server {
    listen 443 ssl;
    server_name $FQDN;
    root /var/www/$FQDN/;
    # Force HTTPS (this may break some websocket clients that try to connect via HTTP)
    if (\$scheme != "https") {
            return 301 https://\$host\$request_uri;
    }
    keepalive_timeout 65;
    keepalive_requests 100000;
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    ssl_protocols TLSv1 TLSv1.1 TLSv1.2;
    ssl_prefer_server_ciphers on;
    ssl_ciphers 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:CAMELLIA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA';
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_stapling on;
    ssl_stapling_verify on;
    add_header Strict-Transport-Security max-age=15768000;
    location / {
        access_log off;
        proxy_pass http://websockets;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_next_upstream     error timeout invalid_header http_500;
        proxy_connect_timeout   2;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
EOL

ln -s /etc/nginx/sites-available/$FQDN /etc/nginx/sites-enabled/$FQDN
# Request a certificate from Let's Encrypt for the fully qualified doname name, allowing certbot to configure nginx automatically
certbot --nginx -d $FQDN -n --agree-tos --email none@here.com
# Append a cronjob to the existing set to renew the certificate every 60 days, then cleanup the temp file
crontab -l > cronjobs; echo '02 02 02 */2 * certbot renew >/dev/null 2>&1' >> cronjobs; crontab cronjobs; rm cronjobs

##################################################################################################
# This VM is now configured as a block producing node. However, it will not sign blocks until    #
# the blochain receives a valid "update_witness" operation contianing the witness name and the   #
# pub_key written into the congif.ini file. The pub_key starts with the prefix 'BTS' (never 5).  #
##################################################################################################
