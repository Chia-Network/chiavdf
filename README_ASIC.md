# ASIC timelord user guide

## Initial setup

### Linux (x86_64)

Preferred: use the helper script to download and stage drivers:
```bash
./scripts/get-libft4222.sh install
```

Manual (if you need to do it by hand):
```bash
# in chiavdf directory
wget https://ftdichip.com/wp-content/uploads/2022/06/libft4222-linux-1.4.4.170.tgz
mkdir src/hw/libft4222
tar -C src/hw/libft4222 -xf libft4222-linux-1.4.4.170.tgz
ln -s libft4222.so.1.4.4.170 src/hw/libft4222/build-x86_64/libft4222.so
```

### macOS (Apple Silicon / arm64)

Preferred: use the helper script to download and stage drivers:
```bash
./scripts/get-libft4222.sh install
```

Manual (if you need to do it by hand):

```bash
# in chiavdf directory
curl -L -o LibFT4222-mac.zip https://ftdichip.com/wp-content/uploads/2024/03/LibFT4222-mac-v1.4.4.190.zip
unzip -q LibFT4222-mac.zip
hdiutil attach -nobrowse -readonly libft4222.1.4.4.190.dmg

mkdir -p src/hw/libft4222
cp /Volumes/ft4222/ftd2xx.h src/hw/libft4222/
cp /Volumes/ft4222/libft4222.h src/hw/libft4222/
cp /Volumes/ft4222/WinTypes.h src/hw/libft4222/
cp /Volumes/ft4222/build/libft4222.1.4.4.190.dylib src/hw/libft4222/
cp /Volumes/ft4222/build/libftd2xx.dylib src/hw/libft4222/
hdiutil detach /Volumes/ft4222

ln -sf libft4222.1.4.4.190.dylib src/hw/libft4222/libft4222.dylib

# Make dylibs relocatable and loadable from the build tree.
install_name_tool -id "@rpath/libftd2xx.dylib" src/hw/libft4222/libftd2xx.dylib
install_name_tool -id "@rpath/libft4222.dylib" src/hw/libft4222/libft4222.1.4.4.190.dylib
install_name_tool -change "libftd2xx.dylib" "@rpath/libftd2xx.dylib" src/hw/libft4222/libft4222.1.4.4.190.dylib

# Clear provenance attributes and ad-hoc sign dylibs to avoid execution kills.
xattr -dr com.apple.provenance src/hw/libft4222
codesign --force --sign - \
  src/hw/libft4222/libftd2xx.dylib \
  src/hw/libft4222/libft4222.1.4.4.190.dylib \
  src/hw/libft4222/libft4222.dylib
```

To clean downloaded artifacts:
```bash
./scripts/get-libft4222.sh clean
```

Build binaries:
```bash
cd src
make -f Makefile.vdf-client emu_hw_test hw_test emu_hw_vdf_client hw_vdf_client
```

Connect the Chia VDF ASIC device and verify that it is detected:
```bash
# in chiavdf/src/ directory
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_vdf_client --list  # Linux
./hw_vdf_client --list                                            # macOS
```

If the device is shown in the list, check if it's working:
```bash
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_test  # Linux
./hw_test                                            # macOS
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
LD_LIBRARY_PATH=hw/libft4222/build-x86_64 ./hw_vdf_client 8000  # Linux
./hw_vdf_client 8000                                            # macOS
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
