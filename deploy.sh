cd /home/kali/Desktop/Operating\ System/
bash clearbuilds.sh
make
mv build/kernel.bin /home/kali/bootloader-project/kernel.bin
cd /home/kali/bootloader-project/
./64bit_uefi_transport_only.sh
thunar
