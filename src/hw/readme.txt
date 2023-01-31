Build instructions:

Start in the top dir of 'chiavdf' repo.

cd src/hw
# create symlink to libftd2xx package
ln -s ../../path/to/libftd2xx
cd ..
make -f Makefile.vdf-client emu_hw_main hw_main emu_hw_vdf_client hw_vdf_client

---

Running with emulated HW:
./emu_hw_main 3 100000

Running with real HW:
./hw_main 3 1000000

The arguments are the number of VDF engines (1, 2 or 3) and the number of iterations to run.

---

To run with an emulated timelord, first build 'chiavdf' Python module:
# start in the top chiavdf directory
python3 setup.py build

Start emulated timelord (replace 3.7 in directory name if your Python version is not 3.7):
PYTHONPATH=build/lib.linux-x86_64-3.7 python3 src/tl_emu.py

Start VDF client in another terminal (8000 is the TCP port number):
./hw_vdf_client 8000

Alternatively, run with the emulated HW:
./emu_hw_vdf_client 8000
