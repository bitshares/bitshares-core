# Руководство по установке и настройке TravelChain на Ubuntu 16.04

## Подготовительные действия

Предположим, что мы имеем выделенный сервер:
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

Итак, у нас в распоряжении есть выделенный сервер с ОС Ubuntu 16.04, 8 процессорами, 16GB ОЗУ, 211GB SSD в программном RAID 1 и внешний IP адрес `1.1.1.256`.

Первым делом необходимо обновить систему и установить пакеты:
```bash
apt update && apt -y upgrade
apt install -y cmake make libbz2-dev libdb++-dev libdb-dev libssl-dev openssl libreadline-dev autoconf libtool git ntp libcurl4-openssl-dev g++ libboost-all-dev lsof
```

Если во время обновления системы устанавливались пакеты ядра, то лучше перезагрузить сервер.

Создадим пользователя `travelchain` который будет работать с TravelChain:
```bash
useradd travelchain
passwd travelchain
chsh -s /bin/bash travelchain
usermod -d /srv travelchain
chown -R travelchain:travelchain /srv/
```

Отключим вход по SSH для `root`:
```bash
vim /etc/ssh/sshd_config
...
PermitRootLogin no
```

Перезапускаем сервис SSH:
```bash
systemctl restart sshd
```

Соединяемся к серверу по SSH под пользователем `travelchain`:
```bash
ssh-copy-id travelchain@1.1.1.256
ssh travelchain@1.1.1.256
```

## Установка TravelChain

Входим в систему из-под пользователя `travelchain`, клонируем репозиторий и устанавливаем ПО:
```bash
su - travelchain
git clone https://github.com/travelchain/travelchain-core.git
cd travelchain-core/ && git submodule update --init --recursive
cmake -DCMAKE_BUILD_TYPE=Release .
make -j$(nproc)
```

## Настройка TravelChain

Инициализируем ноду:
```bash
/srv/travelchain-core/programs/witness_node/witness_node
Ctrl+C
```

Правим конфигурационный файл:
```bash
vim /srv/witness_node_data_dir/config.ini
...
# Подключаемся к ноде WALLET.TRAVELCHAIN.IO
seed-node = 142.44.247.175:4242

# 1000 для ~5GB ОЗУ, 3000 для ~15GB ОЗУ
max-ops-per-account = 3000 

rpc-endpoint = 127.0.0.1:8090
enable-stale-production = true
```

Перезапускаем сервис с параметром `--resync-blockchain`:
```bash
/srv/travelchain-core/programs/witness_node/witness_node --data-dir /srv/witness_node_data_dir/ --resync-blockchain
```

## Настройка кошелька

Пока что не останавливаем `witness_node`.

В новом терминале создаем кошелек и устанавливаем на него пароль:
```bash
/srv/travelchain-core/programs/cli_wallet/cli_wallet
new >>> set_password Ваш_Пар0ль
Ctrl+C
```

Проверяем, создался ли файл `wallet.json`:
```bash
ls -l /srv/wallet.json
-rw------- 1 travelchain travelchain 2374 Dec 20 10:11 /srv/wallet.json
```

Возвращаемся к старой сессии и останавливаем `witness_node`.

## Сервисный файл для systemd

Создаем cервисный файл systemd для `witness_node`:
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

Добавляем сервис в автозагрузку и запускаем его:
```
~# systemctl enable witness_node
~# systemctl start witness_node
```

Проверяем порт `8090`:
```
~# lsof -i :8090
COMMAND     PID        USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
witness_n 15961 travelchain   20u  IPv4 124737      0t0  TCP localhost:8090 (LISTEN)
```

## wallet.travelchain.io

1. Создаем аккаунт на https://wallet.travelchain.io/

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/registration.png)

2. Сохраняем пароль в надежное место 

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/backup.png)

3. Получаем публичный ключ во вкладке `Аккаунт - Права доступа (Account - Permissions)`

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/public_key.png)

4. Жмем на публичный ключ и получаем приватный

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/private_key.png)

## Привязываем кошелек к witness_node

Проверим что `witness_node` работает:
```bash
systemctl is-active witness_node
active
```

Запускаем `cli_wallet` и импортируем ключи:
```bash
/srv/travelchain-core/programs/cli_wallet/cli_wallet
locked >>> unlock Ваш_Пар0ль
unlocked >>> import_key ВашЛогин ВашПриватныйКлюч
unlocked >>> list_my_accounts
unlocked >>> create_witness ВашЛогин "https://www.your_site.com" true
unlocked >>> get_witness ВашЛогин
# Запоминаем значение id (например 1.6.6)
unlocked >>> update_witness ВашЛогин "https://www.your_site.com" "ВашПубличныйКлюч" true
```

Если хотите проголосовать за самого себя (для этого нужны TravelTokens):
```
unlocked >>> vote_for_witness ВашЛогин ВашЛогин true true
Ctrl+C
```

Редактируем файл конфигурации `witness_node`:
```bash
vim /srv/witness_node_data_dir/config.ini
...
private-key = ["ВашПубличныйКлюч", "ВашПриватныйКлюч"]
witness-id = "1.6.6" # Вы же запомнили id?
```

И перезаупскаем`witness_node`:
```bash
su - root
systemctl restart witness_node
```

## Проверяем себя в списке участников

Идем во вкладку `Аккаунт - Голосование (Account - Voting)`

![](https://raw.githubusercontent.com/TravelChain/travelchain-core/master/misc/vote_table.png)

## Решение проблем

* Вы можете запустить `witness node` в сессии screen вместо systemd
```bash
apt install -y screen
su - travelchain
screen -S witness_node
/srv/travelchain-core/programs/witness_node/witness_node --data-dir /srv/witness_node_data_dir/ 
Ctrl+a d
```

* В режиме реального времени можно проверить лог `p2p.log`
```bash
tail -f /srv/witness_node_data_dir/logs/p2p/p2p.log
```

* Не забудьте о резервном копированиии файлов кошелька
```bash
scp /srv/*.wallet backup-server:/backup_dir/
```
