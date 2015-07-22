# Configures Ubuntu 14.04 VM to be used with BitShares 2.0 (Graphene)
# Downloads and builds all necessary software to run witness node and web GUI
# Use with Vagrant (http://docs.vagrantup.com/v2/getting-started/index.html)
# or just execute the shell script below.
# Vagrant setup supports the following providers: Virtual Box, Digital Ocean, Amazon EC2

$script = <<SCRIPT
# ------ shell script begin ------

echo_msg() {
  /bin/echo -e "\e[1;36m*** $1 ***\e[0m"
}

echo_msg "Current user: `id`"
echo_msg "Current dir: `pwd`"

echo_msg "updating system.."
sudo apt-get -y update
sudo apt-get -yfV dist-upgrade

echo_msg "installing required packages.."
sudo apt-get install -yfV git libreadline-dev uuid-dev g++ libdb++-dev libdb-dev zip
sudo apt-get install -yfV libssl-dev openssl build-essential python-dev autotools-dev libicu-dev build-essential
sudo apt-get install -yfV libbz2-dev automake doxygen cmake ncurses-dev libtool nodejs nodejs-legacy npm mc
sudo apt-get -y autoremove

[ ! -d "bts" ] && mkdir bts && cd bts
[ ! -d "tmp" ] && mkdir tmp
[ ! -d "build" ] && mkdir build

if [ ! -d "tmp/boost_1_57_0" ]; then
    echo_msg "building boost.."
    cd tmp/
    wget -nv 'http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.bz2/download'
    tar -xf download
    cd boost_1_57_0/
    ./bootstrap.sh --prefix=/usr/local/ > /dev/null
    sudo ./b2 install > /dev/null
    cd ~/bts
fi 
  
if [ ! -d "graphene" ]; then
  echo_msg "building bitshares graphene toolkit.."  
  git clone https://github.com/cryptonomex/graphene.git
  cd graphene
  git submodule update --init --recursive
  cmake .
  make
  cd ~/bts
fi

if [ ! -d "graphene-ui" ]; then
  echo_msg "installing ui dependencies.."  
  git clone https://github.com/cryptonomex/graphene-ui.git
  cd graphene-ui/dl
  npm install --silent
  cd ../web
  npm install --silent
  npm run-script build
  cd ~/bts
fi

# ------ shell script end ------
SCRIPT



Dir["*.sh"].each {|s| File.open(s,"r"){|f| $script << f.read()} } # includes additional .sh files (plug-ins)

Vagrant.configure(2) do |config|

  config.vm.box = 'ubuntu_trusty_x64'
  config.vm.provision 'shell', inline: $script, privileged: false
  config.ssh.username = 'vagrant'

  # to use with Digital Ocean please install this plugin https://github.com/smdahlen/vagrant-digitalocean
  # note: due to bug in vagrant-digitalocean you need to run provision separetly:
  # vagrant up --provider digital_ocean --no-provision
  # vagrant provision
  config.vm.provider :digital_ocean do |provider, override|
    override.vm.hostname = 'graphene'
    override.ssh.private_key_path = ENV['VAGRANT_KEY_PATH']
    override.vm.box = 'digital_ocean'
    override.vm.box_url = 'https://github.com/smdahlen/vagrant-digitalocean/raw/master/box/digital_ocean.box'
    provider.setup = true
    provider.region = 'nyc2'
    provider.image = 'ubuntu-14-04-x64'
    provider.size = '4GB' # 2GB may be not enought to compile graphene toolkit
    provider.token = ENV['DIGITALOCEAN_TOKEN']
    provider.ssh_key_name = 'vagrant'
  end

  config.vm.provider :aws do |aws, override|
    aws.access_key_id = ENV['AWS_ACCESS_KEY']
    aws.secret_access_key = ENV['AWS_SECRET_KEY']
    aws.keypair_name = $1 if ENV['VAGRANT_KEY_PATH'] =~ /([\w]+)[\.\w]*$/
    aws.region = "us-east-1"
    aws.ami = 'ami-018c9568'
    aws.instance_type = 'm1.small'
    aws.security_groups = [ 'bitsharesxt' ]
    override.vm.hostname = 'bitsharesxt-aws'
    override.ssh.username = 'ubuntu'
    override.ssh.private_key_path = ENV['VAGRANT_KEY_PATH']
    override.vm.box = 'dummy'
  end

  config.vm.provider 'virtualbox' do |v|
    v.customize ['modifyvm', :id, '--memory', '4096']
    v.customize ['modifyvm', :id, '--cpus', 4]
  end

end
