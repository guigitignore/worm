Vagrant.configure("2") do |config|

	config.vm.box= "ubuntu/jammy64"
	config.vm.synced_folder '.', '/vagrant', disabled: true
	config.vm.network "private_network", type: "dhcp"

	config.vm.define "VB1" do |machine|
		#run once trigger
		machine.trigger.before :up do |trigger|
			trigger.info = "Delete ssh_keys"
			trigger.run = {inline: "rm -f id_vb_key id_vb_key.pub"}
		end
	
		machine.trigger.before :up do |trigger|
			trigger.info = "Generate ssh_keys"
			trigger.run = {inline: "ssh-keygen -t ed25519 -f id_vb_key -N ''"}
		end

		machine.vm.provider :virtualbox do |vb|
			vb.name="VB1"
		end
	end

	config.vm.define "VB2" do |machine|
		machine.vm.provider :virtualbox do |vb|
			vb.name="VB2"
		end
	end

	config.vm.provision "upload ssh public key" , type:"file", source: "id_vb_key.pub", destination: ".ssh/id_vb_key.pub"
	config.vm.provision "install shh key",type:"shell", inline: <<-SHELL
    	cat .ssh/id_vb_key.pub >> .ssh/authorized_keys
  	SHELL

	config.vm.provision "display ip address",type:"shell", inline: <<-SHELL
		interfaces=($(ip -o link show | awk -F': ' '{print $2}'))
		last_interface=${interfaces[-1]}
		interface_ip=$(ip -f inet addr show $last_interface | awk '/inet / {print $2}')
		echo "ip address=$interface_ip"
	SHELL

end
