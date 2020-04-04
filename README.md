# md009
md009 board firmware

This project depends on the [secure bootloader for nRF9160](https://github.com/machdep/nrf-boot).

## Create an LFS file system with ceritificates.

### Prepare a 'certs' directory.

It should include these files:
 - certificate.pem
 - private_key.pem
 - public_key.pem
 - rootca.pem

### Build the filesystem and program it to nRF9160
    $ mklfs -c certs -b 512 -r 16 -p 16 -s 16384 -i disk
    $ bin2hex.py --offset=1032192 disk disk.hex
    $ nrfjprog -f NRF91 --erasepage 0xfc000-0x100000
    $ nrfjprog -f NRF91 --program disk.hex

![alt text](https://raw.githubusercontent.com/machdep/nrf9160/master/images/md009.jpg)
