gcc -no-pie -fno-pic -fno-stack-protector -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o build/main.o kernel/main.c 
nasm -f elf -o build/print.o lib/kernel/print.S 
nasm -f elf -o build/kernel.o kernel/kernel.S 
gcc -no-pie -fno-pic -m32 -fno-stack-protector -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o build/interrupt.o kernel/interrupt.c 
gcc -no-pie -fno-pic -m32 -fno-stack-protector -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o build/init.o kernel/init.c 
gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o build/timer.o device/timer.c
ld -m elf_i386 -T kernel/link.script -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/print.o build/kernel.o build/timer.o build/interrupt.o
dd if=./build/kernel.bin of=./bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc