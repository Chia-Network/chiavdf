Build instructions:

Start in the top dir of 'chiavdf' repo.

cd src/hw
# create symlink to libftd2xx package
ln -s ../../path/to/libftd2xx
cd ..
make -f Makefile.vdf-client emu_hw_main hw_main

---

Running with emulated HW:
./emu_hw_main 3 100000

Running with real HW:
./hw_main 3 1000000

The arguments are the number of VDF engines (1, 2 or 3) and the number of iterations to run.
