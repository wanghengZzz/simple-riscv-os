qemu-system-riscv64 -M virt -machine dumpdtb=$1.dtb
dtc -I dtb -O dts -o $1.dts $1.dtb
