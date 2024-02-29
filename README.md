# Worm project

This program scan all hosts in a choosen network searching for hosts with an SSH port opened.

It then copies and executes itself inside the hosts it finds, thanks to a shared public SSH key.

Finally, it lists the users and their properties on the target hosts and stores them in a file.

## Deployment

It is recommended to use `vagrant` to quickly deploy a working infrastructure.

To install vagrant on Debian/Ubuntu: `sudo apt install vagrant`

You can follow instructions on  [vagrant website](https://www.vagrantup.com) otherwise.

Useful commands:

- start machines: `vagrant up`
- stop machines: `vagrant halt`
- destroy machines: `vagrant destroy --force`

## Compiling

You can use `make` to compile the program: `make build`

Using commandline: `gcc -O3 -Wall -W -Wstrict-prototypes -Werror worm.c -o worm`

## Running
