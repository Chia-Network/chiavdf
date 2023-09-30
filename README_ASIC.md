# ASIC timelord user guide

## Initial setup

Download and unpack LibFT4222 library:
```bash
# in chiavdf directory
wget https://ftdichip.com/wp-content/uploads/2022/06/libft4222-linux-1.4.4.170.tgz
mkdir src/hw/libft4222
tar -C src/hw/libft4222 -xf libft4222-linux-1.4.4.170.tgz
ln -s libft4222.so.1.4.4.170 src/hw/libft4222/build-x86_64/libft4222.so
```

Build binaries:
```bash
cd src
make -f Makefile.vdf-client emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client
```

Connect the Chia VDF ASIC device and verify that it is detected:
```bash
# in chiavdf/src/ directory
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_vdf_client --list
```

If the device is shown in the list, check if it's working:
```bash
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_test
```

Output should contain lines similar to the following:
```
VDF 0: 1000000 HW iters done in 2s, HW speed: 714720 ips
```

## Running

You should have a Chia full node running and synced.

Start timelord (but not timelord-launcher) in chia-blockchain:
```bash
chia start timelord-only
```

Start hardware VDF client (`8000` specifies timelord's port number):
```bash
# in chiavdf/src/ directory
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_vdf_client 8000
```

The VDF client accepts a number of options:
```
Usage: ./hw_vdf_client [OPTIONS] PORT [N_VDFS]
List of options [default, min - max]:
  --freq N - set ASIC frequency [1100, 200 - 2200]
  --voltage N - set board voltage [0.88, 0.7 - 1.0]
  --ip A.B.C.D - timelord IP address [localhost]
        Allows connecting to a timelord running on a remote host. Useful when running multiple machines with VDF hardware connecting to a single timelord.
  --vdfs-mask N - mask for enabling VDF engines [7, 1 - 7]
        The ASIC has 3 VDF engines numbered 0, 1, 2. If not running all 3 engines, the mask can be specified to enable specific engines. It must be the result of bitwise OR of the engine bits (1, 2, 4 for engines 0, 1, 2).
  --vdf-threads N - number of software threads per VDF engine [4, 2 - 64]
        Number of software threads computing intermediate values and proofs per VDF engine.
  --proof-threads N - number of proof threads per VDF engine
        Number of software threads only computing proofs per VDF engine. Must be less than --vdf-threads.
  --auto-freq-period N - auto-adjust frequency every N seconds [0, 10 - inf]
  --list - list available devices and exit
```

## Shutting down

Stop timelord:
```bash
chia stop timelord-only
```

Stop the VDF client by pressing Control-C or killing the process with `SIGTERM`.
